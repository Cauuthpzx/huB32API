/**
 * @file test_register_flow.cpp
 * @brief Unit tests for hub32api::api::v1::RegisterController.
 *
 * Tests exercise the full register → verify flow using an in-process
 * DatabaseManager backed by a temporary on-disk SQLite database.
 * httplib::Request and httplib::Response are constructed directly
 * (both are plain structs with no hidden state that blocks instantiation).
 *
 * Test matrix:
 *  1. RegisterCreatesNewTenant          — valid payload → 201, non-empty debugToken
 *  2. RegisterDuplicateEmailRejected    — same email twice → 409
 *  3. VerifyActivatesTenant             — register then verify → tenant status=active
 *  4. VerifyExpiredTokenRejected        — manually insert expired token → 410
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <sqlite3.h>
#include <chrono>

#include <nlohmann/json.hpp>
#include <httplib.h>

#include "db/DatabaseManager.hpp"
#include "db/TenantRepository.hpp"
#include "db/TeacherRepository.hpp"
#include "api/v1/controllers/RegisterController.hpp"

using hub32api::db::DatabaseManager;
using hub32api::db::TenantRepository;
using hub32api::db::TeacherRepository;
using hub32api::api::v1::RegisterController;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class RegisterFlowTest : public ::testing::Test {
protected:
    std::string                        dir;
    std::unique_ptr<DatabaseManager>   dm;
    std::unique_ptr<TenantRepository>  tenantRepo;
    std::unique_ptr<TeacherRepository> teacherRepo;
    std::unique_ptr<RegisterController> ctrl;

    void SetUp() override {
        dir        = "test_data_register_" +
                     std::to_string(reinterpret_cast<uintptr_t>(this));
        dm         = std::make_unique<DatabaseManager>(dir);
        ASSERT_TRUE(dm->isOpen()) << "DatabaseManager failed to open";
        tenantRepo  = std::make_unique<TenantRepository>(*dm);
        teacherRepo = std::make_unique<TeacherRepository>(*dm);
        ctrl        = std::make_unique<RegisterController>(*tenantRepo, *teacherRepo, *dm);
    }

    void TearDown() override {
        ctrl.reset();
        teacherRepo.reset();
        tenantRepo.reset();
        dm.reset();
        std::filesystem::remove_all(dir);
    }

    // Build a minimal POST request with a JSON body.
    static httplib::Request makePostRequest(const nlohmann::json& body) {
        httplib::Request req;
        req.method = "POST";
        req.path   = "/api/v1/register";
        req.body   = body.dump();
        req.set_header("Content-Type", "application/json");
        return req;
    }

    // Build a minimal GET request with query parameters.
    static httplib::Request makeGetRequest(const std::string& path,
                                           const std::string& queryKey,
                                           const std::string& queryValue) {
        httplib::Request req;
        req.method = "GET";
        req.path   = path;
        req.params.emplace(queryKey, queryValue);
        return req;
    }

    // Directly inserts an expired registration token into the DB.
    void insertExpiredToken(const std::string& token, const std::string& tenantId) {
        const int64_t expired = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - 3600; // 1 hour ago

        std::lock_guard<std::mutex> lock(dm->dbMutex());
        sqlite3* db = dm->schoolDb();

        const char* sql = "INSERT INTO registration_tokens(token, tenant_id, expires_at, used)"
                          " VALUES(?, ?, ?, 0);";
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr), SQLITE_OK);
        sqlite3_bind_text(stmt, 1, token.c_str(),    static_cast<int>(token.size()),    SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, expired);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: Successful registration
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief POST /api/v1/register with valid payload must return 201 and a
 *        non-empty debugToken (we are NOT in production mode during tests).
 */
TEST_F(RegisterFlowTest, RegisterCreatesNewTenant)
{
    // Ensure HUB32_ENV is not "production" during tests (unset or anything else)
#ifdef _WIN32
    _putenv_s("HUB32_ENV", "test");
#else
    setenv("HUB32_ENV", "test", 1);
#endif

    const nlohmann::json body = {
        {"orgName",  "Test University"},
        {"email",    "admin@testuniversity.edu"},
        {"password", "securepass123"}
    };

    httplib::Response res;
    ctrl->handleRegister(makePostRequest(body), res);

    EXPECT_EQ(res.status, 201) << "Expected HTTP 201 Created";

    const auto j = nlohmann::json::parse(res.body);
    EXPECT_EQ(j.value("message", ""), "check email");

    const std::string debugToken = j.value("debugToken", "");
    EXPECT_FALSE(debugToken.empty()) << "debugToken must be non-empty in non-production mode";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: Duplicate email rejected
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registering the same email address twice must return 409 Conflict
 *        on the second attempt.
 */
TEST_F(RegisterFlowTest, RegisterDuplicateEmailRejected)
{
#ifdef _WIN32
    _putenv_s("HUB32_ENV", "test");
#else
    setenv("HUB32_ENV", "test", 1);
#endif

    const nlohmann::json body = {
        {"orgName",  "Duplicate Corp"},
        {"email",    "owner@duplicate.com"},
        {"password", "password123"}
    };

    // First registration must succeed
    httplib::Response res1;
    ctrl->handleRegister(makePostRequest(body), res1);
    ASSERT_EQ(res1.status, 201) << "First registration must succeed";

    // Second registration with same email must be rejected
    const nlohmann::json body2 = {
        {"orgName",  "Another Org"},
        {"email",    "owner@duplicate.com"},  // same email
        {"password", "password456"}
    };
    httplib::Response res2;
    ctrl->handleRegister(makePostRequest(body2), res2);
    EXPECT_EQ(res2.status, 409) << "Duplicate email must return 409";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: Full register → verify flow
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Registering then verifying with the returned debug token must
 *        activate the tenant (status becomes "active") and return 200.
 */
TEST_F(RegisterFlowTest, VerifyActivatesTenant)
{
#ifdef _WIN32
    _putenv_s("HUB32_ENV", "test");
#else
    setenv("HUB32_ENV", "test", 1);
#endif

    const nlohmann::json body = {
        {"orgName",  "Verification School"},
        {"email",    "verify@school.org"},
        {"password", "verifypass1"}
    };

    // Step 1: register
    httplib::Response regRes;
    ctrl->handleRegister(makePostRequest(body), regRes);
    ASSERT_EQ(regRes.status, 201) << "Registration must succeed";

    const auto regJson   = nlohmann::json::parse(regRes.body);
    const std::string tok = regJson.value("debugToken", "");
    ASSERT_FALSE(tok.empty()) << "debugToken must be present for verify test to work";

    // Step 2: verify
    httplib::Response verRes;
    ctrl->handleVerify(makeGetRequest("/api/v1/verify", "token", tok), verRes);
    EXPECT_EQ(verRes.status, 200) << "Verification must return 200";

    const auto verJson = nlohmann::json::parse(verRes.body);
    EXPECT_EQ(verJson.value("message", ""), "account activated");

    // Step 3: confirm tenant is now active in DB
    // Retrieve tenant by owner email via slug search (email → slug derived from orgName)
    // Simpler: iterate to find the tenant using TenantRepository::findBySlug
    auto slugResult = tenantRepo->findBySlug("verification-school");
    if (slugResult.is_err()) {
        // Slug might have received a suffix; search by listing all — but there's no listAll.
        // Instead confirm by trying to verify again (should fail with "already used").
        httplib::Response verRes2;
        ctrl->handleVerify(makeGetRequest("/api/v1/verify", "token", tok), verRes2);
        EXPECT_EQ(verRes2.status, 409) << "Second verify must return 409 (token already used)";
    } else {
        EXPECT_EQ(slugResult.value().status, "active") << "Tenant status must be 'active'";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: Expired token rejected
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A registration token that has passed its expiry timestamp must be
 *        rejected with HTTP 410 Gone.
 */
TEST_F(RegisterFlowTest, VerifyExpiredTokenRejected)
{
    // We need a valid tenant for the FK constraint on registration_tokens.
    auto tenantResult = tenantRepo->create("Expired Org", "expired-org", "owner@expired.org");
    ASSERT_TRUE(tenantResult.is_ok()) << "Must be able to create tenant for this test";
    const std::string tenantId = tenantResult.value();

    const std::string expiredToken = "aaaabbbb-cccc-dddd-eeee-ffffffffffff";
    insertExpiredToken(expiredToken, tenantId);

    httplib::Response res;
    ctrl->handleVerify(makeGetRequest("/api/v1/verify", "token", expiredToken), res);

    EXPECT_EQ(res.status, 410) << "Expired token must return 410 Gone";
}
