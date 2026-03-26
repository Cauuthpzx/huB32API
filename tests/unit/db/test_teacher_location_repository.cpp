/**
 * @file test_teacher_location_repository.cpp
 * @brief Unit tests for hub32api::db::TeacherLocationRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"
#include "db/LocationRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "db/TeacherLocationRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::SchoolRepository;
using hub32api::db::LocationRepository;
using hub32api::db::TeacherRepository;
using hub32api::db::TeacherLocationRepository;

class TeacherLocationRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<SchoolRepository> schoolRepo;
    std::unique_ptr<LocationRepository> locationRepo;
    std::unique_ptr<TeacherRepository> teacherRepo;
    std::unique_ptr<TeacherLocationRepository> repo;

    void SetUp() override {
        dir = "test_data_teacher_location_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";

        schoolRepo = std::make_unique<SchoolRepository>(dm->schoolDb());
        locationRepo = std::make_unique<LocationRepository>(dm->schoolDb());
        teacherRepo = std::make_unique<TeacherRepository>(dm->schoolDb());
        repo = std::make_unique<TeacherLocationRepository>(dm->schoolDb());
    }

    void TearDown() override {
        repo.reset();
        teacherRepo.reset();
        locationRepo.reset();
        schoolRepo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

// ---------------------------------------------------------------------------
// Test: Assign
// ---------------------------------------------------------------------------

TEST_F(TeacherLocationRepositoryTest, Assign)
{
    // Create school
    auto schoolResult = schoolRepo->create("Test School", "123 Test St");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    // Create location
    auto locationResult = locationRepo->create(schoolId, "Lab A", "Building 1", 2, 30, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Create teacher
    auto teacherResult = teacherRepo->create("teacher1", "password123", "Teacher One", "teacher");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string teacherId = teacherResult.value();

    // Assign teacher to location
    auto assignResult = repo->assign(teacherId, locationId);
    ASSERT_TRUE(assignResult.is_ok()) << "assign() must succeed";

    // Verify hasAccess returns true
    bool hasAccess = repo->hasAccess(teacherId, locationId);
    EXPECT_TRUE(hasAccess) << "hasAccess() must return true after assign";
}

// ---------------------------------------------------------------------------
// Test: Revoke
// ---------------------------------------------------------------------------

TEST_F(TeacherLocationRepositoryTest, Revoke)
{
    // Create school
    auto schoolResult = schoolRepo->create("Test School", "123 Test St");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    // Create location
    auto locationResult = locationRepo->create(schoolId, "Lab A", "Building 1", 2, 30, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Create teacher
    auto teacherResult = teacherRepo->create("teacher2", "password123", "Teacher Two", "teacher");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string teacherId = teacherResult.value();

    // Assign teacher to location
    auto assignResult = repo->assign(teacherId, locationId);
    ASSERT_TRUE(assignResult.is_ok());
    EXPECT_TRUE(repo->hasAccess(teacherId, locationId));

    // Revoke access
    auto revokeResult = repo->revoke(teacherId, locationId);
    ASSERT_TRUE(revokeResult.is_ok()) << "revoke() must succeed";

    // Verify hasAccess returns false
    bool hasAccess = repo->hasAccess(teacherId, locationId);
    EXPECT_FALSE(hasAccess) << "hasAccess() must return false after revoke";
}

// ---------------------------------------------------------------------------
// Test: HasAccessFalse
// ---------------------------------------------------------------------------

TEST_F(TeacherLocationRepositoryTest, HasAccessFalse)
{
    // Create school
    auto schoolResult = schoolRepo->create("Test School", "123 Test St");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    // Create location
    auto locationResult = locationRepo->create(schoolId, "Lab A", "Building 1", 2, 30, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Create teacher (but do NOT assign)
    auto teacherResult = teacherRepo->create("teacher3", "password123", "Teacher Three", "teacher");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string teacherId = teacherResult.value();

    // Verify hasAccess returns false (no assignment exists)
    bool hasAccess = repo->hasAccess(teacherId, locationId);
    EXPECT_FALSE(hasAccess) << "hasAccess() must return false when no assignment exists";
}

// ---------------------------------------------------------------------------
// Test: GetLocationsForTeacher
// ---------------------------------------------------------------------------

TEST_F(TeacherLocationRepositoryTest, GetLocationsForTeacher)
{
    // Create school
    auto schoolResult = schoolRepo->create("Test School", "123 Test St");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    // Create two locations
    auto loc1Result = locationRepo->create(schoolId, "Lab A", "Building 1", 2, 30, "lab");
    ASSERT_TRUE(loc1Result.is_ok());
    const std::string locationId1 = loc1Result.value();

    auto loc2Result = locationRepo->create(schoolId, "Lab B", "Building 2", 2, 25, "lab");
    ASSERT_TRUE(loc2Result.is_ok());
    const std::string locationId2 = loc2Result.value();

    // Create teacher
    auto teacherResult = teacherRepo->create("teacher4", "password123", "Teacher Four", "teacher");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string teacherId = teacherResult.value();

    // Assign teacher to both locations
    auto assign1Result = repo->assign(teacherId, locationId1);
    ASSERT_TRUE(assign1Result.is_ok());
    auto assign2Result = repo->assign(teacherId, locationId2);
    ASSERT_TRUE(assign2Result.is_ok());

    // Get locations for teacher
    std::vector<std::string> locationIds = repo->getLocationIdsForTeacher(teacherId);
    EXPECT_EQ(locationIds.size(), size_t{2}) << "getLocationIdsForTeacher() must return 2 locations";
}

// ---------------------------------------------------------------------------
// Test: GetTeachersForLocation
// ---------------------------------------------------------------------------

TEST_F(TeacherLocationRepositoryTest, GetTeachersForLocation)
{
    // Create school
    auto schoolResult = schoolRepo->create("Test School", "123 Test St");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    // Create location
    auto locationResult = locationRepo->create(schoolId, "Lab A", "Building 1", 2, 30, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Create two teachers
    auto teacher1Result = teacherRepo->create("teacher5", "password123", "Teacher Five", "teacher");
    ASSERT_TRUE(teacher1Result.is_ok());
    const std::string teacherId1 = teacher1Result.value();

    auto teacher2Result = teacherRepo->create("teacher6", "password123", "Teacher Six", "teacher");
    ASSERT_TRUE(teacher2Result.is_ok());
    const std::string teacherId2 = teacher2Result.value();

    // Assign both teachers to the location
    auto assign1Result = repo->assign(teacherId1, locationId);
    ASSERT_TRUE(assign1Result.is_ok());
    auto assign2Result = repo->assign(teacherId2, locationId);
    ASSERT_TRUE(assign2Result.is_ok());

    // Get teachers for location
    std::vector<std::string> teacherIds = repo->getTeacherIdsForLocation(locationId);
    EXPECT_EQ(teacherIds.size(), size_t{2}) << "getTeacherIdsForLocation() must return 2 teachers";
}
