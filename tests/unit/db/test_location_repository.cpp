/**
 * @file test_location_repository.cpp
 * @brief Unit tests for hub32api::db::LocationRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 * A school record is created as a prerequisite for location tests.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"
#include "db/LocationRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::SchoolRepository;
using hub32api::db::LocationRepository;
using hub32api::ErrorCode;

class LocationRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager>   dm;
    std::unique_ptr<SchoolRepository>  schoolRepo;
    std::unique_ptr<LocationRepository> repo;

    std::string schoolAId;
    std::string schoolBId;

    void SetUp() override {
        dir = "test_data_location_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm         = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        schoolRepo = std::make_unique<SchoolRepository>(*dm);
        repo       = std::make_unique<LocationRepository>(*dm);

        // Create prerequisite schools used by multiple tests
        auto rA = schoolRepo->create("School A", "1 Alpha St");
        ASSERT_TRUE(rA.is_ok());
        schoolAId = rA.value();

        auto rB = schoolRepo->create("School B", "2 Beta St");
        ASSERT_TRUE(rB.is_ok());
        schoolBId = rB.value();
    }

    void TearDown() override {
        repo.reset();
        schoolRepo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

// ---------------------------------------------------------------------------
// Test: CreateAndFind
// ---------------------------------------------------------------------------

TEST_F(LocationRepositoryTest, CreateAndFind)
{
    auto createResult = repo->create(schoolAId, "Room 101", "Main Building", 1, 30, "classroom");
    ASSERT_TRUE(createResult.is_ok()) << "create() must succeed";

    const std::string id = createResult.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for a just-created location";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.schoolId, schoolAId);
    EXPECT_EQ(rec.name,     "Room 101");
    EXPECT_EQ(rec.building, "Main Building");
    EXPECT_EQ(rec.floor,    1);
    EXPECT_EQ(rec.capacity, 30);
    EXPECT_EQ(rec.type,     "classroom");
}

// ---------------------------------------------------------------------------
// Test: ListBySchool
// ---------------------------------------------------------------------------

TEST_F(LocationRepositoryTest, ListBySchool)
{
    // 2 locations in school A
    auto r1 = repo->create(schoolAId, "Lab A1", "Science Wing", 2, 20, "lab");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = repo->create(schoolAId, "Lab A2", "Science Wing", 2, 20, "lab");
    ASSERT_TRUE(r2.is_ok());

    // 1 location in school B
    auto r3 = repo->create(schoolBId, "Office B1", "Admin Block", 0, 5, "office");
    ASSERT_TRUE(r3.is_ok());

    auto listA = repo->listBySchool(schoolAId);
    ASSERT_TRUE(listA.is_ok()) << "listBySchool(A) must succeed";
    EXPECT_EQ(listA.value().size(), size_t{2})
        << "listBySchool(A) must return exactly 2 locations";

    auto listB = repo->listBySchool(schoolBId);
    ASSERT_TRUE(listB.is_ok()) << "listBySchool(B) must succeed";
    EXPECT_EQ(listB.value().size(), size_t{1})
        << "listBySchool(B) must return exactly 1 location";
}

// ---------------------------------------------------------------------------
// Test: ListAll
// ---------------------------------------------------------------------------

TEST_F(LocationRepositoryTest, ListAll)
{
    // 2 in A, 1 in B → 3 total
    auto r1 = repo->create(schoolAId, "Room 1", "Bldg A", 1, 25, "classroom");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = repo->create(schoolAId, "Room 2", "Bldg A", 2, 25, "classroom");
    ASSERT_TRUE(r2.is_ok());
    auto r3 = repo->create(schoolBId, "Lab 1", "Bldg B", 1, 15, "lab");
    ASSERT_TRUE(r3.is_ok());

    auto listResult = repo->listAll();
    ASSERT_TRUE(listResult.is_ok()) << "listAll() must succeed";
    EXPECT_EQ(listResult.value().size(), size_t{3})
        << "listAll() must return exactly 3 locations";
}

// ---------------------------------------------------------------------------
// Test: Update
// ---------------------------------------------------------------------------

TEST_F(LocationRepositoryTest, Update)
{
    auto createResult = repo->create(schoolAId, "Old Room", "Old Bldg", 0, 10, "classroom");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto updateResult = repo->update(id, "New Room", "New Bldg", 3, 40, "lab");
    ASSERT_TRUE(updateResult.is_ok()) << "update() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    const auto& rec = findResult.value();
    EXPECT_EQ(rec.name,     "New Room");
    EXPECT_EQ(rec.building, "New Bldg");
    EXPECT_EQ(rec.floor,    3);
    EXPECT_EQ(rec.capacity, 40);
    EXPECT_EQ(rec.type,     "lab");
}

// ---------------------------------------------------------------------------
// Test: Delete
// ---------------------------------------------------------------------------

TEST_F(LocationRepositoryTest, Delete)
{
    auto createResult = repo->create(schoolAId, "To Delete", "Block X", 1, 5, "office");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto removeResult = repo->remove(id);
    ASSERT_TRUE(removeResult.is_ok()) << "remove() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_err()) << "findById() must fail after deletion";
    EXPECT_EQ(findResult.error().code, ErrorCode::NotFound);
}
