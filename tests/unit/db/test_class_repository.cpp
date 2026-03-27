/**
 * @file test_class_repository.cpp
 * @brief Unit tests for hub32api::db::ClassRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 *
 * Foreign key enforcement is disabled via PRAGMA so tests can insert
 * stub tenant/school rows without requiring full TenantRepository /
 * SchoolRepository setup.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/ClassRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::ClassRepository;
using hub32api::db::ClassRecord;
using hub32api::ErrorCode;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ClassRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<ClassRepository> repo;

    // Fixed stub IDs used across tests (FK enforcement is OFF)
    static constexpr const char* kTenantA  = "tenant-a-0000-0000-0000-000000000001";
    static constexpr const char* kTenantB  = "tenant-b-0000-0000-0000-000000000002";
    static constexpr const char* kSchoolA  = "school-a-0000-0000-0000-000000000001";
    static constexpr const char* kTeacherA = "teacher-a-000-0000-0000-000000000001";
    static constexpr const char* kTeacherB = "teacher-b-000-0000-0000-000000000002";

    void SetUp() override {
        dir  = "test_data_class_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm   = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";

        // Disable FK enforcement so we can use stub tenant/school IDs
        sqlite3_exec(dm->schoolDb(), "PRAGMA foreign_keys=OFF;", nullptr, nullptr, nullptr);

        repo = std::make_unique<ClassRepository>(*dm);
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

TEST_F(ClassRepositoryTest, CreateReturnsValidId)
{
    auto result = repo->create(kTenantA, kSchoolA, "Math 101", "");
    ASSERT_TRUE(result.is_ok()) << "create() must succeed";

    const std::string id = result.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";
}

// ---------------------------------------------------------------------------
// Test: FindByIdAfterCreate
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, FindByIdAfterCreate)
{
    auto createResult = repo->create(kTenantA, kSchoolA, "Physics Advanced", kTeacherA);
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for a just-created class";

    const ClassRecord& rec = findResult.value();
    EXPECT_EQ(rec.id,        id);
    EXPECT_EQ(rec.tenantId,  kTenantA);
    EXPECT_EQ(rec.schoolId,  kSchoolA);
    EXPECT_EQ(rec.name,      "Physics Advanced");
    EXPECT_EQ(rec.teacherId, kTeacherA);
    EXPECT_GT(rec.createdAt, int64_t{0}) << "created_at must be a positive epoch timestamp";
}

// ---------------------------------------------------------------------------
// Test: FindByIdNoTeacher — nullable teacherId reads as empty string
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, FindByIdNoTeacher)
{
    auto createResult = repo->create(kTenantA, kSchoolA, "Art Class", "");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_TRUE(findResult.value().teacherId.empty())
        << "teacherId must be empty when stored as NULL";
}

// ---------------------------------------------------------------------------
// Test: ListByTenantReturnsCorrectClasses
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, ListByTenantReturnsCorrectClasses)
{
    // Two classes for tenantA, one for tenantB
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class A1", "").is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class A2", "").is_ok());
    ASSERT_TRUE(repo->create(kTenantB, kSchoolA, "Class B1", "").is_ok());

    auto listResult = repo->listByTenant(kTenantA);
    ASSERT_TRUE(listResult.is_ok()) << "listByTenant() must succeed";
    EXPECT_EQ(listResult.value().size(), size_t{2})
        << "listByTenant() must return exactly 2 classes for tenantA";

    for (const auto& rec : listResult.value()) {
        EXPECT_EQ(rec.tenantId, kTenantA);
    }
}

// ---------------------------------------------------------------------------
// Test: ListByTeacherReturnsCorrectClasses
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, ListByTeacherReturnsCorrectClasses)
{
    // Two classes for teacherA, one for teacherB, one with no teacher
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class T-A1", kTeacherA).is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class T-A2", kTeacherA).is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class T-B1", kTeacherB).is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kSchoolA, "Class No Teacher", "").is_ok());

    auto listResult = repo->listByTeacher(kTeacherA);
    ASSERT_TRUE(listResult.is_ok()) << "listByTeacher() must succeed";
    EXPECT_EQ(listResult.value().size(), size_t{2})
        << "listByTeacher() must return exactly 2 classes for teacherA";

    for (const auto& rec : listResult.value()) {
        EXPECT_EQ(rec.teacherId, kTeacherA);
    }
}

// ---------------------------------------------------------------------------
// Test: UpdateChangesName
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, UpdateChangesName)
{
    auto createResult = repo->create(kTenantA, kSchoolA, "Old Name", kTeacherA);
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto updateResult = repo->update(id, "New Name", kTeacherA);
    ASSERT_TRUE(updateResult.is_ok()) << "update() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().name, "New Name");
    EXPECT_EQ(findResult.value().teacherId, kTeacherA);
}

// ---------------------------------------------------------------------------
// Test: UpdateNotFound
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, UpdateNotFound)
{
    auto result = repo->update("nonexistent-id-12345", "New Name", "");
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Test: UpdateClearsTeacher — setting teacherId to empty stores NULL
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, UpdateClearsTeacher)
{
    auto createResult = repo->create(kTenantA, kSchoolA, "Science", kTeacherA);
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto updateResult = repo->update(id, "Science", "");
    ASSERT_TRUE(updateResult.is_ok()) << "update() with empty teacherId must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_TRUE(findResult.value().teacherId.empty())
        << "teacherId must be empty after clearing";
}

// ---------------------------------------------------------------------------
// Test: RemoveDeletesClass
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, RemoveDeletesClass)
{
    auto createResult = repo->create(kTenantA, kSchoolA, "To Be Deleted", "");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto removeResult = repo->remove(id);
    ASSERT_TRUE(removeResult.is_ok()) << "remove() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_err()) << "findById() must fail after deletion";
    EXPECT_EQ(findResult.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Test: RemoveNotFound
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, RemoveNotFound)
{
    auto result = repo->remove("00000000-0000-0000-0000-000000000000");
    ASSERT_TRUE(result.is_err()) << "remove() on non-existent id must fail";
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Test: TenantIsolation — findById returns the record regardless of which
//       tenant the caller represents (isolation is enforced at controller level)
// ---------------------------------------------------------------------------

TEST_F(ClassRepositoryTest, TenantIsolation)
{
    // Create a class belonging to tenantA
    auto createResult = repo->create(kTenantA, kSchoolA, "Isolated Class", "");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // findById with the same id must succeed — the repo does a raw lookup by id
    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok())
        << "findById() must succeed regardless of caller's tenant context";
    EXPECT_EQ(findResult.value().tenantId, kTenantA)
        << "Record must carry the correct tenant_id for upper-layer enforcement";
}
