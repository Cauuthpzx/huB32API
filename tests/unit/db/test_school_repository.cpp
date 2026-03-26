/**
 * @file test_school_repository.cpp
 * @brief Unit tests for hub32api::db::SchoolRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::SchoolRepository;
using hub32api::ErrorCode;

class SchoolRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<SchoolRepository> repo;

    void SetUp() override {
        dir = "test_data_school_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm   = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        repo = std::make_unique<SchoolRepository>(dm->schoolDb());
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

TEST_F(SchoolRepositoryTest, CreateAndFind)
{
    auto createResult = repo->create("Greenwood High", "123 Elm Street");
    ASSERT_TRUE(createResult.is_ok()) << "create() must succeed";

    const std::string id = createResult.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for a just-created school";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,      id);
    EXPECT_EQ(rec.name,    "Greenwood High");
    EXPECT_EQ(rec.address, "123 Elm Street");
    EXPECT_GT(rec.createdAt, int64_t{0}) << "created_at must be a positive epoch timestamp";
}

// ---------------------------------------------------------------------------
// Test: ListAll
// ---------------------------------------------------------------------------

TEST_F(SchoolRepositoryTest, ListAll)
{
    auto r1 = repo->create("School Alpha", "1 Alpha Ave");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = repo->create("School Beta", "2 Beta Blvd");
    ASSERT_TRUE(r2.is_ok());

    auto listResult = repo->listAll();
    ASSERT_TRUE(listResult.is_ok()) << "listAll() must succeed";
    EXPECT_EQ(listResult.value().size(), size_t{2}) << "listAll() must return exactly 2 schools";
}

// ---------------------------------------------------------------------------
// Test: Update
// ---------------------------------------------------------------------------

TEST_F(SchoolRepositoryTest, Update)
{
    auto createResult = repo->create("Old Name", "Old Address");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto updateResult = repo->update(id, "New Name", "New Address");
    ASSERT_TRUE(updateResult.is_ok()) << "update() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().name,    "New Name");
    EXPECT_EQ(findResult.value().address, "New Address");
}

// ---------------------------------------------------------------------------
// Test: Delete
// ---------------------------------------------------------------------------

TEST_F(SchoolRepositoryTest, Delete)
{
    auto createResult = repo->create("To Be Deleted", "Nowhere Lane");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto removeResult = repo->remove(id);
    ASSERT_TRUE(removeResult.is_ok()) << "remove() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_err()) << "findById() must fail after deletion";
    EXPECT_EQ(findResult.error().code, ErrorCode::NotFound);
}
