/**
 * @file test_computer_repository.cpp
 * @brief Unit tests for hub32api::db::ComputerRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 * A school and location are created as prerequisites where needed.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"
#include "db/LocationRepository.hpp"
#include "db/ComputerRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::SchoolRepository;
using hub32api::db::LocationRepository;
using hub32api::db::ComputerRepository;
using hub32api::ErrorCode;

class ComputerRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager>    dm;
    std::unique_ptr<SchoolRepository>   schoolRepo;
    std::unique_ptr<LocationRepository> locationRepo;
    std::unique_ptr<ComputerRepository> repo;

    std::string locationAId;
    std::string locationBId;

    void SetUp() override {
        dir = "test_data_computer_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm           = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        schoolRepo   = std::make_unique<SchoolRepository>(dm->schoolDb());
        locationRepo = std::make_unique<LocationRepository>(dm->schoolDb());
        repo         = std::make_unique<ComputerRepository>(dm->schoolDb());

        // Create a school to satisfy the FK on locations
        auto schoolResult = schoolRepo->create("Test School", "1 Test Ave");
        ASSERT_TRUE(schoolResult.is_ok());
        const std::string schoolId = schoolResult.value();

        // Create two locations used by multiple tests
        auto rA = locationRepo->create(schoolId, "Lab A", "Block A", 1, 30, "lab");
        ASSERT_TRUE(rA.is_ok());
        locationAId = rA.value();

        auto rB = locationRepo->create(schoolId, "Lab B", "Block B", 2, 20, "lab");
        ASSERT_TRUE(rB.is_ok());
        locationBId = rB.value();
    }

    void TearDown() override {
        repo.reset();
        locationRepo.reset();
        schoolRepo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }
};

// ---------------------------------------------------------------------------
// Test: CreateAndFind
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, CreateAndFind)
{
    auto createResult = repo->create(locationAId, "pc-lab-a-01", "AA:BB:CC:DD:EE:01");
    ASSERT_TRUE(createResult.is_ok()) << "create() must succeed";

    const std::string id = createResult.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for a just-created computer";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,         id);
    EXPECT_EQ(rec.locationId, locationAId);
    EXPECT_EQ(rec.hostname,   "pc-lab-a-01");
    EXPECT_EQ(rec.macAddress, "AA:BB:CC:DD:EE:01");
    EXPECT_EQ(rec.state,      "offline");
    EXPECT_EQ(rec.lastHeartbeat, int64_t{0});
}

// ---------------------------------------------------------------------------
// Test: CreateUnassigned
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, CreateUnassigned)
{
    auto createResult = repo->createUnassigned("pc-unassigned", "FF:EE:DD:CC:BB:AA");
    ASSERT_TRUE(createResult.is_ok()) << "createUnassigned() must succeed";

    const std::string id = createResult.value();
    EXPECT_FALSE(id.empty());

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed";

    const auto& rec = findResult.value();
    EXPECT_TRUE(rec.locationId.empty()) << "locationId must be empty for unassigned computer";
    EXPECT_EQ(rec.hostname,   "pc-unassigned");
    EXPECT_EQ(rec.macAddress, "FF:EE:DD:CC:BB:AA");
    EXPECT_EQ(rec.state,      "offline");
}

// ---------------------------------------------------------------------------
// Test: FindByHostname
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, FindByHostname)
{
    auto createResult = repo->create(locationAId, "pc-hostname-test", "11:22:33:44:55:66");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    auto findResult = repo->findByHostname("pc-hostname-test");
    ASSERT_TRUE(findResult.is_ok()) << "findByHostname() must succeed";

    const auto& rec = findResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.hostname, "pc-hostname-test");
}

// ---------------------------------------------------------------------------
// Test: ListByLocation
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, ListByLocation)
{
    // 2 computers in location A
    auto r1 = repo->create(locationAId, "pc-a-01", "AA:AA:AA:AA:AA:01");
    ASSERT_TRUE(r1.is_ok());
    auto r2 = repo->create(locationAId, "pc-a-02", "AA:AA:AA:AA:AA:02");
    ASSERT_TRUE(r2.is_ok());

    // 1 computer in location B
    auto r3 = repo->create(locationBId, "pc-b-01", "BB:BB:BB:BB:BB:01");
    ASSERT_TRUE(r3.is_ok());

    auto listA = repo->listByLocation(locationAId);
    ASSERT_TRUE(listA.is_ok()) << "listByLocation(A) must succeed";
    EXPECT_EQ(listA.value().size(), size_t{2})
        << "listByLocation(A) must return exactly 2 computers";

    auto listB = repo->listByLocation(locationBId);
    ASSERT_TRUE(listB.is_ok()) << "listByLocation(B) must succeed";
    EXPECT_EQ(listB.value().size(), size_t{1})
        << "listByLocation(B) must return exactly 1 computer";
}

// ---------------------------------------------------------------------------
// Test: UpdateState
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, UpdateState)
{
    auto createResult = repo->create(locationAId, "pc-state-test", "DE:AD:BE:EF:00:01");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // Verify initial state
    {
        auto findResult = repo->findById(id);
        ASSERT_TRUE(findResult.is_ok());
        EXPECT_EQ(findResult.value().state, "offline");
    }

    auto updateResult = repo->updateState(id, "online");
    ASSERT_TRUE(updateResult.is_ok()) << "updateState() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().state, "online");
}

// ---------------------------------------------------------------------------
// Test: UpdateHeartbeat
// ---------------------------------------------------------------------------

TEST_F(ComputerRepositoryTest, UpdateHeartbeat)
{
    auto createResult = repo->create(locationAId, "pc-hb-test", "CA:FE:BA:BE:00:01");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // Verify initial heartbeat is 0
    {
        auto findResult = repo->findById(id);
        ASSERT_TRUE(findResult.is_ok());
        EXPECT_EQ(findResult.value().lastHeartbeat, int64_t{0});
    }

    auto hbResult = repo->updateHeartbeat(id, "192.168.1.42", "1.2.3");
    ASSERT_TRUE(hbResult.is_ok()) << "updateHeartbeat() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    const auto& rec = findResult.value();
    EXPECT_EQ(rec.ipLastSeen,   "192.168.1.42");
    EXPECT_EQ(rec.agentVersion, "1.2.3");
    EXPECT_GT(rec.lastHeartbeat, int64_t{0}) << "lastHeartbeat must be a positive epoch timestamp";
    EXPECT_EQ(rec.state, "online") << "state must be set to online after heartbeat";
}
