/**
 * @file test_agent_db_integration.cpp
 * @brief End-to-end integration tests for the agent lifecycle against a real database.
 *
 * These tests do NOT start an HTTP server. Instead they directly use repository
 * and registry classes to verify database integration: agent registration,
 * heartbeat state updates, location access control, feature command queueing,
 * and computer assignment to locations.
 *
 * Each test fixture creates its own DatabaseManager in a unique temp directory
 * and removes it in TearDown() to prevent cross-test interference.
 */

#include <gtest/gtest.h>
#include <filesystem>

#include "db/DatabaseManager.hpp"
#include "db/SchoolRepository.hpp"
#include "db/LocationRepository.hpp"
#include "db/ComputerRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "db/TeacherLocationRepository.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"

using namespace hub32api;
using namespace hub32api::db;
using namespace hub32api::agent;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class AgentDbIntegrationTest : public ::testing::Test {
protected:
    std::string dataDir;
    std::unique_ptr<DatabaseManager>            dm;
    std::unique_ptr<SchoolRepository>           schoolRepo;
    std::unique_ptr<LocationRepository>         locationRepo;
    std::unique_ptr<ComputerRepository>         computerRepo;
    std::unique_ptr<TeacherRepository>          teacherRepo;
    std::unique_ptr<TeacherLocationRepository>  teacherLocationRepo;
    AgentRegistry agentRegistry;

    void SetUp() override {
        dataDir = "test_agent_integration_" +
                  std::to_string(reinterpret_cast<uintptr_t>(this));
        dm = std::make_unique<DatabaseManager>(dataDir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";

        schoolRepo          = std::make_unique<SchoolRepository>(*dm);
        locationRepo        = std::make_unique<LocationRepository>(*dm);
        computerRepo        = std::make_unique<ComputerRepository>(*dm);
        teacherRepo         = std::make_unique<TeacherRepository>(*dm);
        teacherLocationRepo = std::make_unique<TeacherLocationRepository>(*dm);
    }

    void TearDown() override {
        // Destroy in reverse construction order before removing files.
        teacherLocationRepo.reset();
        teacherRepo.reset();
        computerRepo.reset();
        locationRepo.reset();
        schoolRepo.reset();
        dm.reset();
        std::filesystem::remove_all(dataDir);
    }
};

// ---------------------------------------------------------------------------
// Test 1: AgentRegistrationCreatesComputer
//
// Verifies that after an agent registers in the AgentRegistry, the server
// can create a corresponding unassigned computer record (mirroring what
// AgentController does) and find it by hostname.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, AgentRegistrationCreatesComputer)
{
    const std::string agentId  = "agent-reg-001";
    const std::string hostname = "PC-Lab-Reg-01";

    // Step 1: Register the agent in the in-memory registry.
    AgentInfo info;
    info.agentId  = agentId;
    info.hostname = hostname;
    info.ipAddress = "10.0.0.1";

    auto regResult = agentRegistry.registerAgent(info);
    ASSERT_TRUE(regResult.is_ok()) << "registerAgent() must succeed";
    EXPECT_EQ(regResult.value(), agentId);

    // Step 2: Simulate what AgentController does — persist to DB.
    auto createResult = computerRepo->createUnassigned(hostname, "");
    ASSERT_TRUE(createResult.is_ok()) << "createUnassigned() must succeed";

    // Step 3: Verify the computer is findable by hostname.
    auto findResult = computerRepo->findByHostname(hostname);
    ASSERT_TRUE(findResult.is_ok()) << "findByHostname() must succeed";
    EXPECT_EQ(findResult.value().hostname, hostname);

    // Step 4: Default state must be "offline".
    EXPECT_EQ(findResult.value().state, "offline")
        << "Newly created computer state must default to offline";
}

// ---------------------------------------------------------------------------
// Test 2: HeartbeatUpdatesComputerState
//
// Verifies that calling updateHeartbeat() sets ip and version, timestamps
// lastHeartbeat, and transitions the computer to the "online" state.
// Then updateState() can change the state independently.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, HeartbeatUpdatesComputerState)
{
    // Step 1: Create computer.
    auto createResult = computerRepo->createUnassigned("PC-HB-01", "AA:BB:CC:DD:EE:FF");
    ASSERT_TRUE(createResult.is_ok());
    const std::string id = createResult.value();

    // Step 2: Call updateHeartbeat.
    auto hbResult = computerRepo->updateHeartbeat(id, "192.168.10.5", "2.0.0");
    ASSERT_TRUE(hbResult.is_ok()) << "updateHeartbeat() must succeed";

    // Step 3: Verify ip, version, and that lastHeartbeat was set.
    {
        auto findResult = computerRepo->findById(id);
        ASSERT_TRUE(findResult.is_ok());
        const auto& rec = findResult.value();
        EXPECT_EQ(rec.ipLastSeen,   "192.168.10.5");
        EXPECT_EQ(rec.agentVersion, "2.0.0");
        EXPECT_GT(rec.lastHeartbeat, int64_t{0})
            << "lastHeartbeat must be a positive epoch timestamp after heartbeat";
        EXPECT_EQ(rec.state, "online")
            << "updateHeartbeat() must transition state to online";
    }

    // Step 4: updateState() can also change state independently.
    auto stateResult = computerRepo->updateState(id, "locked");
    ASSERT_TRUE(stateResult.is_ok()) << "updateState() must succeed";

    auto findResult = computerRepo->findById(id);
    ASSERT_TRUE(findResult.is_ok());
    EXPECT_EQ(findResult.value().state, "locked");
}

// ---------------------------------------------------------------------------
// Test 3: LocationAccessControl
//
// Verifies that teacher access to a location can be granted and revoked
// through TeacherLocationRepository.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, LocationAccessControl)
{
    // Step 1: Create school and location.
    auto schoolResult = schoolRepo->create("Integration School", "1 Test Ave");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    auto locationResult = locationRepo->create(schoolId, "Lab Integration", "Block C", 1, 25, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Step 2: Create teacher with role "teacher".
    auto teacherResult = teacherRepo->create("teacher.access", "pass1234", "Teacher Access", "teacher");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string teacherId = teacherResult.value();

    // Step 3: Create a computer in the location (prerequisite for completeness).
    auto computerResult = computerRepo->create(locationId, "PC-Access-01", "00:11:22:33:44:55");
    ASSERT_TRUE(computerResult.is_ok());

    // Step 4: Initially hasAccess must be false.
    {
        auto access = teacherLocationRepo->hasAccess(teacherId, locationId);
        ASSERT_TRUE(access.is_ok());
        EXPECT_FALSE(access.value())
            << "Teacher must NOT have access before assignment";
    }

    // Step 5: Assign and verify access.
    auto assignResult = teacherLocationRepo->assign(teacherId, locationId);
    ASSERT_TRUE(assignResult.is_ok()) << "assign() must succeed";
    {
        auto access = teacherLocationRepo->hasAccess(teacherId, locationId);
        ASSERT_TRUE(access.is_ok());
        EXPECT_TRUE(access.value())
            << "Teacher must have access after assignment";
    }

    // Step 6: Revoke and verify access is removed.
    auto revokeResult = teacherLocationRepo->revoke(teacherId, locationId);
    ASSERT_TRUE(revokeResult.is_ok()) << "revoke() must succeed";
    {
        auto access = teacherLocationRepo->hasAccess(teacherId, locationId);
        ASSERT_TRUE(access.is_ok());
        EXPECT_FALSE(access.value())
            << "Teacher must NOT have access after revoke";
    }
}

// ---------------------------------------------------------------------------
// Test 4: AdminBypassesLocationCheck
//
// Verifies that a teacher with role "admin" can be created and queried.
// The access bypass itself is implemented in Router / middleware, so this
// test only verifies the role attribute round-trips correctly through the DB.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, AdminBypassesLocationCheck)
{
    // Step 1: Create school → location → computer (full chain).
    auto schoolResult = schoolRepo->create("Admin School", "2 Admin Rd");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    auto locationResult = locationRepo->create(schoolId, "Admin Lab", "Block D", 0, 20, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    auto computerResult = computerRepo->create(locationId, "PC-Admin-01", "FF:EE:DD:CC:BB:AA");
    ASSERT_TRUE(computerResult.is_ok());

    // Step 2: Create teacher with role "admin".
    auto teacherResult = teacherRepo->create("admin.user", "adminpass", "Admin User", "admin");
    ASSERT_TRUE(teacherResult.is_ok());
    const std::string adminId = teacherResult.value();

    // Step 3: Verify role is stored and retrievable correctly.
    auto findResult = teacherRepo->findById(adminId);
    ASSERT_TRUE(findResult.is_ok()) << "findById() must succeed for admin";
    EXPECT_EQ(findResult.value().role, "admin")
        << "Admin role must be persisted correctly";

    // Step 4: Admin has no explicit location assignment yet.
    // The TeacherLocationRepository reflects this — admin bypass is in Router.
    {
        auto access = teacherLocationRepo->hasAccess(adminId, locationId);
        ASSERT_TRUE(access.is_ok());
        EXPECT_FALSE(access.value())
            << "No DB-level assignment exists; Router-level bypass handles admin access";
    }
}

// ---------------------------------------------------------------------------
// Test 5: FeatureCommandQueueing
//
// Verifies the full command lifecycle: queue → dequeue → report result → find.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, FeatureCommandQueueing)
{
    const std::string agentId  = "agent-cmd-001";
    const std::string commandId = "cmd-db-int-001";

    // Step 1: Register the agent.
    AgentInfo info;
    info.agentId  = agentId;
    info.hostname = "PC-Cmd-01";
    info.ipAddress = "10.0.1.1";
    auto regResult = agentRegistry.registerAgent(info);
    ASSERT_TRUE(regResult.is_ok());

    // Step 2: Queue a command.
    AgentCommand cmd;
    cmd.commandId  = commandId;
    cmd.agentId    = agentId;
    cmd.featureUid = "lock-screen";
    cmd.operation  = "start";
    agentRegistry.queueCommand(cmd);

    // Step 3: Dequeue pending commands — should contain exactly one.
    auto pending = agentRegistry.dequeuePendingCommands(agentId);
    ASSERT_EQ(pending.size(), 1u) << "Exactly one command must be pending";
    EXPECT_EQ(pending[0].commandId,  commandId);
    EXPECT_EQ(pending[0].featureUid, "lock-screen");
    EXPECT_EQ(pending[0].operation,  "start");

    // Dequeue again: queue must now be empty.
    auto empty = agentRegistry.dequeuePendingCommands(agentId);
    EXPECT_EQ(empty.size(), 0u) << "Command queue must be empty after dequeue";

    // Step 4: Report result.
    agentRegistry.reportCommandResult(commandId, CommandStatus::Success, "ok", 100);

    // Step 5: Find the command and verify its final state.
    auto found = agentRegistry.findCommand(commandId);
    ASSERT_TRUE(found.is_ok()) << "findCommand() must succeed after result reported";
    EXPECT_EQ(found.value().status,     CommandStatus::Success);
    EXPECT_EQ(found.value().durationMs, 100);
    EXPECT_EQ(found.value().result,     "ok");
}

// ---------------------------------------------------------------------------
// Test 6: ComputerAssignToLocation
//
// Verifies that an unassigned computer can be assigned to a location via
// update() and then appears in listByLocation().
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, ComputerAssignToLocation)
{
    // Step 1: Create school and location.
    auto schoolResult = schoolRepo->create("Assign School", "3 Assign Blvd");
    ASSERT_TRUE(schoolResult.is_ok());
    const std::string schoolId = schoolResult.value();

    auto locationResult = locationRepo->create(schoolId, "Assign Lab", "Block E", 2, 30, "lab");
    ASSERT_TRUE(locationResult.is_ok());
    const std::string locationId = locationResult.value();

    // Step 2: Create an unassigned computer.
    auto createResult = computerRepo->createUnassigned("PC-Assign-01", "DE:AD:BE:EF:CA:FE");
    ASSERT_TRUE(createResult.is_ok());
    const std::string computerId = createResult.value();

    // Verify it is not in the location yet.
    {
        auto list = computerRepo->listByLocation(locationId);
        ASSERT_TRUE(list.is_ok());
        EXPECT_EQ(list.value().size(), 0u)
            << "No computers should be in the location before assignment";
    }

    // Step 3: Assign the computer to the location via update().
    auto updateResult = computerRepo->update(computerId, locationId, "PC-Assign-01", 0, 0);
    ASSERT_TRUE(updateResult.is_ok()) << "update() must succeed";

    // Step 4: Verify listByLocation() now includes the computer.
    auto list = computerRepo->listByLocation(locationId);
    ASSERT_TRUE(list.is_ok()) << "listByLocation() must succeed";
    ASSERT_EQ(list.value().size(), 1u)
        << "listByLocation() must return exactly 1 computer after assignment";
    EXPECT_EQ(list.value()[0].id,       computerId);
    EXPECT_EQ(list.value()[0].hostname, "PC-Assign-01");
    EXPECT_EQ(list.value()[0].locationId, locationId);
}

// ---------------------------------------------------------------------------
// Test 7: FullLifecycle_RegisterHeartbeatOffline
//
// Verifies the full agent lifecycle: register → heartbeat → offline → recover.
// Covers DB creation, location assignment, state transitions, and IP/version
// updates across multiple heartbeats.
// ---------------------------------------------------------------------------

TEST_F(AgentDbIntegrationTest, FullLifecycle_RegisterHeartbeatOffline)
{
    // 1. Create a school + location for the computer
    auto schoolId = schoolRepo->create("Test School", "123 Main St");
    ASSERT_TRUE(schoolId.is_ok());
    auto locId = locationRepo->create(schoolId.value(), "Lab A", "Building 1", 1, 30, "lab");
    ASSERT_TRUE(locId.is_ok());

    // 2. Register agent (creates unassigned computer)
    hub32api::AgentInfo info;
    info.agentId = "agent-lifecycle-test";
    info.hostname = "PC-LIFECYCLE-01";
    info.ipAddress = "192.168.1.50";
    info.agentVersion = "2.0.0";
    info.state = hub32api::AgentState::Online;
    auto regResult = agentRegistry.registerAgent(info);
    ASSERT_TRUE(regResult.is_ok());

    // Create computer in DB (simulating what AgentController does)
    auto compId = computerRepo->createUnassigned("PC-LIFECYCLE-01", "AA:BB:CC:DD:EE:FF");
    ASSERT_TRUE(compId.is_ok());

    // 3. Assign computer to location
    auto updateResult = computerRepo->update(compId.value(), locId.value(), "PC-LIFECYCLE-01", 0, 0);
    ASSERT_TRUE(updateResult.is_ok());

    // 4. Heartbeat — should set state to online
    computerRepo->updateHeartbeat(compId.value(), "192.168.1.50", "2.0.0");
    auto comp = computerRepo->findById(compId.value());
    ASSERT_TRUE(comp.is_ok());
    EXPECT_EQ(comp.value().state, "online");
    EXPECT_EQ(comp.value().ipLastSeen, "192.168.1.50");
    EXPECT_EQ(comp.value().agentVersion, "2.0.0");

    // 5. Verify computer is in location
    auto inLoc = computerRepo->listByLocation(locId.value());
    ASSERT_TRUE(inLoc.is_ok());
    EXPECT_EQ(inLoc.value().size(), 1u);
    EXPECT_EQ(inLoc.value()[0].hostname, "PC-LIFECYCLE-01");

    // 6. Simulate offline — manually set state
    computerRepo->updateState(compId.value(), "offline");
    auto offComp = computerRepo->findById(compId.value());
    ASSERT_TRUE(offComp.is_ok());
    EXPECT_EQ(offComp.value().state, "offline");

    // 7. Heartbeat again — should come back online
    computerRepo->updateHeartbeat(compId.value(), "192.168.1.51", "2.0.1");
    auto backOnline = computerRepo->findById(compId.value());
    ASSERT_TRUE(backOnline.is_ok());
    EXPECT_EQ(backOnline.value().state, "online");
    EXPECT_EQ(backOnline.value().ipLastSeen, "192.168.1.51");
    EXPECT_EQ(backOnline.value().agentVersion, "2.0.1");
}
