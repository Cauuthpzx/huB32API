/**
 * @file test_student_repository.cpp
 * @brief Unit tests for hub32api::db::StudentRepository.
 *
 * Each test creates its own DatabaseManager in a unique temp directory and
 * cleans up in TearDown() to prevent cross-test interference.
 *
 * PRAGMA foreign_keys=OFF is used so tests can insert students with stub
 * tenant/class IDs without requiring full TenantRepository/ClassRepository setup.
 *
 * Anti-enumeration contract: authenticate() MUST return the same ErrorCode
 * (AuthenticationFailed) and the same message ("Invalid credentials") for both
 * "user not found" and "wrong password" cases.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>

#include "db/DatabaseManager.hpp"
#include "db/StudentRepository.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::StudentRepository;
using hub32api::db::StudentRecord;
using hub32api::ErrorCode;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class StudentRepositoryTest : public ::testing::Test {
protected:
    std::string dir;
    std::unique_ptr<DatabaseManager> dm;
    std::unique_ptr<StudentRepository> repo;

    // Fixed stub IDs — foreign key enforcement is OFF in SetUp()
    static constexpr const char* kTenantA  = "tenant-a-0000-0000-0000-000000000001";
    static constexpr const char* kTenantB  = "tenant-b-0000-0000-0000-000000000002";
    static constexpr const char* kClassA   = "class-a-0000-0000-0000-000000000001";
    static constexpr const char* kClassB   = "class-b-0000-0000-0000-000000000002";
    static constexpr const char* kMachineA = "machine-aa11-0000-0000-000000000001";

    void SetUp() override {
        dir = "test_data_student_" + std::to_string(reinterpret_cast<uintptr_t>(this));
        dm  = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";

        // Disable FK enforcement so we can use stub tenant/class IDs
        sqlite3_exec(dm->schoolDb(), "PRAGMA foreign_keys=OFF;", nullptr, nullptr, nullptr);

        repo = std::make_unique<StudentRepository>(*dm);
    }

    void TearDown() override {
        repo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }

    // Convenience: create a student and assert success, return the UUID
    std::string createStudent(const char* tenantId = kTenantA,
                               const char* classId  = kClassA,
                               const char* fullName = "Alice Nguyen",
                               const char* username = "alice",
                               const char* password = "P@ssw0rd!")
    {
        auto result = repo->create(tenantId, classId, fullName, username, password);
        if (!result.is_ok()) {
            ADD_FAILURE() << "createStudent helper failed: " << result.error().message;
            return {};
        }
        return result.value();
    }
};

// ---------------------------------------------------------------------------
// Test 1: CreateReturnsValidId
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, CreateReturnsValidId)
{
    auto result = repo->create(kTenantA, kClassA, "Alice Nguyen", "alice", "P@ssw0rd!");
    ASSERT_TRUE(result.is_ok()) << "create() must succeed";

    const std::string id = result.value();
    EXPECT_FALSE(id.empty()) << "Returned id must not be empty";
    // A UUID is 36 characters (with hyphens)
    EXPECT_EQ(id.size(), size_t{36}) << "Returned id must be a 36-char UUID";
}

// ---------------------------------------------------------------------------
// Test 2: AuthenticateSuccess
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, AuthenticateSuccess)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    auto authResult = repo->authenticate("alice", "P@ssw0rd!", kTenantA);
    ASSERT_TRUE(authResult.is_ok()) << "authenticate() must succeed with correct credentials";

    const StudentRecord& rec = authResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.username, "alice");
    EXPECT_EQ(rec.tenantId, kTenantA);
    EXPECT_EQ(rec.classId,  kClassA);
    EXPECT_EQ(rec.fullName, "Alice Nguyen");
    // password_hash must NOT be present (StudentRecord has no such field)
    EXPECT_FALSE(rec.isActivated);
}

// ---------------------------------------------------------------------------
// Test 3: AuthenticateBadPassword
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, AuthenticateBadPassword)
{
    createStudent();

    auto authResult = repo->authenticate("alice", "wrongpassword", kTenantA);
    ASSERT_TRUE(authResult.is_err()) << "authenticate() must fail with wrong password";
    EXPECT_EQ(authResult.error().code, ErrorCode::AuthenticationFailed);
    EXPECT_EQ(authResult.error().message, "Invalid credentials")
        << "Anti-enumeration: error message must be identical for wrong password";
}

// ---------------------------------------------------------------------------
// Test 4: AuthenticateNotFound — same error as wrong password (anti-enumeration)
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, AuthenticateNotFound)
{
    // No student created; attempt to authenticate a non-existent username
    auto authResult = repo->authenticate("ghost", "anypassword", kTenantA);
    ASSERT_TRUE(authResult.is_err()) << "authenticate() must fail for non-existent username";
    EXPECT_EQ(authResult.error().code, ErrorCode::AuthenticationFailed)
        << "Anti-enumeration: code must be AuthenticationFailed (not NotFound)";
    EXPECT_EQ(authResult.error().message, "Invalid credentials")
        << "Anti-enumeration: message must be identical to wrong-password case";
}

// ---------------------------------------------------------------------------
// Test 5: ActivateBindsMachine
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ActivateBindsMachine)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    auto activateResult = repo->activate(id, kMachineA);
    ASSERT_TRUE(activateResult.is_ok()) << "activate() must succeed on first call";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());

    const StudentRecord& rec = findResult.value();
    EXPECT_TRUE(rec.isActivated)       << "isActivated must be true after activation";
    EXPECT_EQ(rec.machineId, kMachineA) << "machineId must match the provided machine identifier";
    EXPECT_GT(rec.activatedAt, int64_t{0}) << "activatedAt must be a positive epoch timestamp";
}

// ---------------------------------------------------------------------------
// Test 6: ActivateAlreadyActivated — second call must return Conflict
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ActivateAlreadyActivated)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    ASSERT_TRUE(repo->activate(id, kMachineA).is_ok())
        << "First activate() must succeed";

    auto secondResult = repo->activate(id, "machine-bb22-0000-0000-000000000002");
    ASSERT_TRUE(secondResult.is_err()) << "Second activate() must fail";
    EXPECT_EQ(secondResult.error().code, ErrorCode::Conflict)
        << "Already-activated error must be Conflict";
}

// ---------------------------------------------------------------------------
// Test 7: ResetMachineClearsBinding
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ResetMachineClearsBinding)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    ASSERT_TRUE(repo->activate(id, kMachineA).is_ok());

    auto resetResult = repo->resetMachine(id);
    ASSERT_TRUE(resetResult.is_ok()) << "resetMachine() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());

    const StudentRecord& rec = findResult.value();
    EXPECT_FALSE(rec.isActivated)      << "isActivated must be false after reset";
    EXPECT_TRUE(rec.machineId.empty()) << "machineId must be empty after reset";
    EXPECT_EQ(rec.activatedAt, int64_t{0}) << "activatedAt must be 0 after reset";
}

// ---------------------------------------------------------------------------
// Test 8: ChangePasswordResetsBinding
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ChangePasswordResetsBinding)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    ASSERT_TRUE(repo->activate(id, kMachineA).is_ok())
        << "Precondition: student must be activated";

    auto changeResult = repo->changePassword(id, "NewP@ssw0rd#2");
    ASSERT_TRUE(changeResult.is_ok()) << "changePassword() must succeed";

    auto findResult = repo->findById(id);
    ASSERT_TRUE(findResult.is_ok());

    const StudentRecord& rec = findResult.value();
    EXPECT_FALSE(rec.isActivated)      << "isActivated must be false after password change";
    EXPECT_TRUE(rec.machineId.empty()) << "machineId must be cleared after password change";

    // Old password must no longer work
    auto oldAuthResult = repo->authenticate("alice", "P@ssw0rd!", kTenantA);
    EXPECT_TRUE(oldAuthResult.is_err()) << "Old password must be rejected after change";

    // New password must work
    auto newAuthResult = repo->authenticate("alice", "NewP@ssw0rd#2", kTenantA);
    EXPECT_TRUE(newAuthResult.is_ok()) << "New password must authenticate successfully";
}

// ---------------------------------------------------------------------------
// Test 9: RemoveNotFound
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, RemoveNotFound)
{
    auto result = repo->remove("00000000-0000-0000-0000-000000000000");
    ASSERT_TRUE(result.is_err()) << "remove() on non-existent id must fail";
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Test 10: UsernameUniquePerTenant
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, UsernameUniquePerTenant)
{
    // First student in tenantA — must succeed
    auto first = repo->create(kTenantA, kClassA, "Alice First",  "alice", "password1");
    ASSERT_TRUE(first.is_ok()) << "First create() must succeed";

    // Second student with the same username in the SAME tenant — must fail with Conflict
    auto second = repo->create(kTenantA, kClassA, "Alice Second", "alice", "password2");
    ASSERT_TRUE(second.is_err()) << "Duplicate username in same tenant must fail";
    EXPECT_EQ(second.error().code, ErrorCode::Conflict)
        << "Duplicate username must return Conflict";

    // Same username in a DIFFERENT tenant — must succeed (unique per tenant, not globally)
    auto third = repo->create(kTenantB, kClassB, "Alice Other",  "alice", "password3");
    ASSERT_TRUE(third.is_ok())
        << "Same username in a different tenant must be allowed";
}

// ---------------------------------------------------------------------------
// Additional: ListByClass returns correct students
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ListByClassReturnsCorrectStudents)
{
    // Two students in classA, one in classB
    ASSERT_TRUE(repo->create(kTenantA, kClassA, "Charlie Brown", "charlie", "pw1").is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kClassA, "Bob Smith",     "bob",     "pw2").is_ok());
    ASSERT_TRUE(repo->create(kTenantA, kClassB, "Dave Jones",    "dave",    "pw3").is_ok());

    auto listResult = repo->listByClass(kClassA);
    ASSERT_TRUE(listResult.is_ok()) << "listByClass() must succeed";

    const auto& students = listResult.value();
    ASSERT_EQ(students.size(), size_t{2})
        << "listByClass() must return exactly 2 students for classA";

    // Verify ordering: results are sorted by full_name
    EXPECT_EQ(students[0].fullName, "Bob Smith")
        << "First result must be Bob Smith (alphabetically first)";
    EXPECT_EQ(students[1].fullName, "Charlie Brown")
        << "Second result must be Charlie Brown";

    for (const auto& rec : students) {
        EXPECT_EQ(rec.classId, kClassA);
    }
}

// ---------------------------------------------------------------------------
// Additional: FindByUsername returns correct record
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, FindByUsernameReturnsCorrectRecord)
{
    const std::string id = createStudent();
    ASSERT_FALSE(id.empty());

    auto findResult = repo->findByUsername(kTenantA, "alice");
    ASSERT_TRUE(findResult.is_ok()) << "findByUsername() must succeed for existing student";

    const StudentRecord& rec = findResult.value();
    EXPECT_EQ(rec.id,       id);
    EXPECT_EQ(rec.username, "alice");
    EXPECT_EQ(rec.tenantId, kTenantA);
}

// ---------------------------------------------------------------------------
// Additional: FindByUsername NotFound
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, FindByUsernameNotFound)
{
    auto findResult = repo->findByUsername(kTenantA, "nobody");
    ASSERT_TRUE(findResult.is_err());
    EXPECT_EQ(findResult.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Additional: ResetMachineNotFound
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ResetMachineNotFound)
{
    auto result = repo->resetMachine("00000000-0000-0000-0000-000000000000");
    ASSERT_TRUE(result.is_err()) << "resetMachine() on non-existent id must fail";
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Additional: ChangePasswordNotFound
// ---------------------------------------------------------------------------

TEST_F(StudentRepositoryTest, ChangePasswordNotFound)
{
    auto result = repo->changePassword("00000000-0000-0000-0000-000000000000", "newpass");
    ASSERT_TRUE(result.is_err()) << "changePassword() on non-existent id must fail";
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}
