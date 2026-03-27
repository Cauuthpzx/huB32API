/**
 * @file StudentRepository.cpp
 * @brief CRUD and authentication operations for the students table.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction (prevents SQL injection).
 *
 * Password hashing delegates entirely to UserRoleStore::hashPassword() and
 * UserRoleStore::verifyPassword() (argon2id / PBKDF2 depending on build config).
 *
 * Anti-enumeration: authenticate() returns the SAME error code and message for
 * "user not found" and "wrong password". Callers MUST NOT distinguish between them.
 */

#include "../core/PrecompiledHeader.hpp"
#include "StudentRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"
#include "../auth/UserRoleStore.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;
using hub32api::auth::UserRoleStore;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

StudentRepository::StudentRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// Helper: build a StudentRecord from a prepared statement row (no hash column)
//
// Expected column order (0-based):
//   0  id
//   1  tenant_id
//   2  class_id
//   3  full_name
//   4  username
//   5  machine_id      (nullable TEXT)
//   6  is_activated    (INTEGER)
//   7  activated_at    (nullable INTEGER)
//   8  created_at      (INTEGER)
// ---------------------------------------------------------------------------

namespace {

StudentRecord record_from_stmt(sqlite3_stmt* stmt)
{
    auto col_text = [&](int col) -> std::string {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return txt ? std::string(txt) : std::string{};
    };

    StudentRecord rec;
    rec.id          = col_text(0);
    rec.tenantId    = col_text(1);
    rec.classId     = col_text(2);
    rec.fullName    = col_text(3);
    rec.username    = col_text(4);
    rec.machineId   = (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
                        ? col_text(5)
                        : std::string{};
    rec.isActivated = (sqlite3_column_int(stmt, 6) != 0);
    rec.activatedAt = (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
                        ? sqlite3_column_int64(stmt, 7)
                        : int64_t{0};
    rec.createdAt   = sqlite3_column_int64(stmt, 8);
    return rec;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new student, generating a UUID and hashing the password.
 *
 * @param tenantId  UUID of the owning tenant.
 * @param classId   UUID of the class this student belongs to.
 * @param fullName  Display name of the student.
 * @param username  Login username — must be unique within the tenant.
 * @param password  Plaintext password (hashed before storage; never stored raw).
 * @return Result containing the new student UUID, or Conflict if username is taken.
 */
Result<std::string> StudentRepository::create(const std::string& tenantId,
                                               const std::string& classId,
                                               const std::string& fullName,
                                               const std::string& username,
                                               const std::string& password)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    // Hash password first — fail early before touching the database
    auto hashResult = UserRoleStore::hashPassword(password);
    if (hashResult.is_err()) {
        spdlog::error("[StudentRepository] password hash failed");
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "Password hash failed"
        });
    }
    const std::string passwordHash = hashResult.take();

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[StudentRepository] UUID generation failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "INSERT INTO students(id, tenant_id, class_id, full_name, username, password_hash, created_at)"
        " VALUES(?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),           static_cast<int>(id.size()),           SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tenantId.c_str(),     static_cast<int>(tenantId.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, classId.c_str(),      static_cast<int>(classId.size()),      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fullName.c_str(),     static_cast<int>(fullName.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, username.c_str(),     static_cast<int>(username.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, passwordHash.c_str(), static_cast<int>(passwordHash.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 7, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_CONSTRAINT) {
        const int extErr = sqlite3_extended_errcode(m_db);
        if (extErr == SQLITE_CONSTRAINT_UNIQUE) {
            return Result<std::string>::fail(ApiError{
                ErrorCode::Conflict,
                "Username already taken in this tenant: " + username
            });
        }
        spdlog::error("[StudentRepository] create constraint error (ext={}): {}", extErr, sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[StudentRepository] created student id={} username={}", id, username);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// findByUsernameWithHash  (internal — never expose hash to callers)
// ---------------------------------------------------------------------------

/**
 * @brief Fetches a student row including the password hash for authentication purposes.
 *
 * This method is intentionally private. The password hash MUST NOT leave this
 * translation unit — it is only compared in authenticate() and then discarded.
 */
Result<StudentRepository::StudentWithHash>
StudentRepository::findByUsernameWithHash(const std::string& tenantId, const std::string& username)
{
    // NOTE: caller must already hold m_dbManager.dbMutex()

    constexpr const char* k_sql =
        "SELECT id, tenant_id, class_id, full_name, username, password_hash,"
        "       machine_id, is_activated, activated_at, created_at"
        " FROM students WHERE tenant_id=? AND username=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] findByUsernameWithHash prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<StudentWithHash>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), static_cast<int>(username.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // col 5 = password_hash; cols 6-9 map to the helper's indices 5-8
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };

        StudentWithHash swh;
        swh.passwordHash = col_text(5);

        // Build the public record (without hash).
        // The helper record_from_stmt() expects columns at indices 0,1,2,3,4,5,6,7,8.
        // Here we have an extra column at index 5 (password_hash), so map manually.
        swh.record.id          = col_text(0);
        swh.record.tenantId    = col_text(1);
        swh.record.classId     = col_text(2);
        swh.record.fullName    = col_text(3);
        swh.record.username    = col_text(4);
        swh.record.machineId   = (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
                                   ? col_text(6)
                                   : std::string{};
        swh.record.isActivated = (sqlite3_column_int(stmt, 7) != 0);
        swh.record.activatedAt = (sqlite3_column_type(stmt, 8) != SQLITE_NULL)
                                   ? sqlite3_column_int64(stmt, 8)
                                   : int64_t{0};
        swh.record.createdAt   = sqlite3_column_int64(stmt, 9);

        sqlite3_finalize(stmt);
        return Result<StudentWithHash>::ok(std::move(swh));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<StudentWithHash>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found"
        });
    }

    spdlog::error("[StudentRepository] findByUsernameWithHash step failed: {}", sqlite3_errmsg(m_db));
    return Result<StudentWithHash>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// authenticate
// ---------------------------------------------------------------------------

/**
 * @brief Authenticates a student by username/password within a tenant.
 *
 * SECURITY — Anti-enumeration: both "user not found" and "wrong password" return
 * ErrorCode::AuthenticationFailed with the message "Invalid credentials".
 * This prevents attackers from probing which usernames exist in the system.
 *
 * @param username  Student login name.
 * @param password  Plaintext password to verify.
 * @param tenantId  Tenant scope for the lookup.
 * @return Result containing StudentRecord (no hash) on success.
 */
Result<StudentRecord> StudentRepository::authenticate(const std::string& username,
                                                       const std::string& password,
                                                       const std::string& tenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto findResult = findByUsernameWithHash(tenantId, username);
    if (findResult.is_err()) {
        // Return the SAME error regardless of whether the user was not found or any DB error.
        // This is intentional: do not reveal whether the username exists.
        spdlog::debug("[StudentRepository] authenticate: user not found (username={})", username);
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid credentials"
        });
    }

    auto swh = findResult.take();

    if (!UserRoleStore::verifyPassword(password, swh.passwordHash)) {
        spdlog::debug("[StudentRepository] authenticate: wrong password (username={})", username);
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid credentials"
        });
    }

    spdlog::debug("[StudentRepository] authenticate: success id={}", swh.record.id);
    return Result<StudentRecord>::ok(std::move(swh.record));
}

// ---------------------------------------------------------------------------
// activate
// ---------------------------------------------------------------------------

/**
 * @brief Binds the student to a specific machine (one-time activation).
 *
 * Returns Conflict if the student is already activated to prevent double-binding.
 *
 * @param studentId  UUID of the student to activate.
 * @param machineId  Unique identifier of the machine being bound.
 */
Result<void> StudentRepository::activate(const std::string& studentId,
                                          const std::string& machineId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    // Step 1: check current activation state
    constexpr const char* k_check_sql =
        "SELECT is_activated FROM students WHERE id=? LIMIT 1;";

    sqlite3_stmt* chk = nullptr;
    if (sqlite3_prepare_v2(m_db, k_check_sql, -1, &chk, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] activate check prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(chk, 1, studentId.c_str(), static_cast<int>(studentId.size()), SQLITE_STATIC);

    const int chk_rc = sqlite3_step(chk);
    if (chk_rc == SQLITE_DONE) {
        sqlite3_finalize(chk);
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + studentId
        });
    }
    if (chk_rc != SQLITE_ROW) {
        spdlog::error("[StudentRepository] activate check step failed: {}", sqlite3_errmsg(m_db));
        sqlite3_finalize(chk);
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    const bool alreadyActivated = (sqlite3_column_int(chk, 0) != 0);
    sqlite3_finalize(chk);

    if (alreadyActivated) {
        return Result<void>::fail(ApiError{
            ErrorCode::Conflict,
            "Machine already activated for this student"
        });
    }

    // Step 2: perform the activation update
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "UPDATE students SET machine_id=?, is_activated=1, activated_at=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] activate update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, machineId.c_str(), static_cast<int>(machineId.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, now);
    sqlite3_bind_text(stmt, 3, studentId.c_str(), static_cast<int>(studentId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] activate update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[StudentRepository] activated student id={} machineId={}", studentId, machineId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// resetMachine
// ---------------------------------------------------------------------------

/**
 * @brief Clears machine binding so the student can activate on a new machine.
 *
 * Sets machine_id=NULL, is_activated=0, activated_at=NULL.
 * Returns NotFound if no row was modified.
 *
 * @param studentId  UUID of the student whose binding should be cleared.
 */
Result<void> StudentRepository::resetMachine(const std::string& studentId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE students SET machine_id=NULL, is_activated=0, activated_at=NULL WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] resetMachine prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, studentId.c_str(), static_cast<int>(studentId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] resetMachine step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + studentId
        });
    }

    spdlog::debug("[StudentRepository] resetMachine id={}", studentId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// changePassword
// ---------------------------------------------------------------------------

/**
 * @brief Changes the student's password and simultaneously resets machine binding.
 *
 * Resetting the machine binding forces the student to re-activate after a password
 * change, which ensures compromised credentials cannot be used to resume a session
 * on a bound machine without re-authentication.
 *
 * @param studentId   UUID of the student.
 * @param newPassword Plaintext new password (hashed before storage).
 */
Result<void> StudentRepository::changePassword(const std::string& studentId,
                                                const std::string& newPassword)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto hashResult = UserRoleStore::hashPassword(newPassword);
    if (hashResult.is_err()) {
        spdlog::error("[StudentRepository] changePassword hash failed");
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            "Password hash failed"
        });
    }
    const std::string newHash = hashResult.take();

    constexpr const char* k_sql =
        "UPDATE students"
        " SET password_hash=?, machine_id=NULL, is_activated=0, activated_at=NULL"
        " WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] changePassword prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, newHash.c_str(),    static_cast<int>(newHash.size()),    SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, studentId.c_str(),  static_cast<int>(studentId.size()),  SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] changePassword step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + studentId
        });
    }

    spdlog::debug("[StudentRepository] changePassword id={}", studentId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// applyPasswordHash
// ---------------------------------------------------------------------------

/**
 * @brief Writes a pre-computed argon2id hash directly into password_hash,
 *        and resets machine binding so the student must re-activate.
 *
 * Called by RequestController when a pending change_password ticket is accepted.
 * The hash was computed at ticket-submission time and stored in pending_requests.payload.
 */
Result<void> StudentRepository::applyPasswordHash(const std::string& studentId,
                                                   const std::string& passwordHash)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE students"
        " SET password_hash=?, machine_id=NULL, is_activated=0, activated_at=NULL"
        " WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] applyPasswordHash prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, passwordHash.c_str(),
                      static_cast<int>(passwordHash.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, studentId.c_str(),
                      static_cast<int>(studentId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] applyPasswordHash step failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Student not found: " + studentId
        });
    }

    spdlog::debug("[StudentRepository] applied password hash for student id={}", studentId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Looks up a student by primary key.
 *
 * Does NOT return the password_hash — use findByUsernameWithHash() internally
 * for authentication only.
 *
 * @param id  UUID of the student.
 * @return Result containing StudentRecord, or NotFound if not present.
 */
Result<StudentRecord> StudentRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, class_id, full_name, username,"
        "       machine_id, is_activated, activated_at, created_at"
        " FROM students WHERE id=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        StudentRecord rec = record_from_stmt(stmt);
        sqlite3_finalize(stmt);
        return Result<StudentRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + id
        });
    }

    spdlog::error("[StudentRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<StudentRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// findByUsername
// ---------------------------------------------------------------------------

/**
 * @brief Looks up a student by (tenantId, username).
 *
 * Does NOT return the password_hash.
 *
 * @param tenantId  UUID of the tenant scope.
 * @param username  Login name of the student.
 * @return Result containing StudentRecord, or NotFound if not present.
 */
Result<StudentRecord> StudentRepository::findByUsername(const std::string& tenantId,
                                                         const std::string& username)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, class_id, full_name, username,"
        "       machine_id, is_activated, activated_at, created_at"
        " FROM students WHERE tenant_id=? AND username=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] findByUsername prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(), static_cast<int>(username.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        StudentRecord rec = record_from_stmt(stmt);
        sqlite3_finalize(stmt);
        return Result<StudentRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<StudentRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found"
        });
    }

    spdlog::error("[StudentRepository] findByUsername step failed: {}", sqlite3_errmsg(m_db));
    return Result<StudentRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// listByClass
// ---------------------------------------------------------------------------

/**
 * @brief Returns all students in a class, ordered alphabetically by full_name.
 *
 * @param classId  UUID of the class.
 * @return Result containing a (possibly empty) vector of StudentRecord.
 */
Result<std::vector<StudentRecord>> StudentRepository::listByClass(const std::string& classId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, class_id, full_name, username,"
        "       machine_id, is_activated, activated_at, created_at"
        " FROM students WHERE class_id=? ORDER BY full_name;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] listByClass prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<StudentRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, classId.c_str(), static_cast<int>(classId.size()), SQLITE_STATIC);

    std::vector<StudentRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        result.push_back(record_from_stmt(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] listByClass step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<StudentRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<StudentRecord>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates the student's display name.
 *
 * @param id       UUID of the student.
 * @param fullName New display name.
 * @return Result<void> — fails with NotFound if no row was modified.
 */
Result<void> StudentRepository::update(const std::string& id, const std::string& fullName)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE students SET full_name=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, fullName.c_str(), static_cast<int>(fullName.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + id
        });
    }

    spdlog::debug("[StudentRepository] updated student id={} fullName={}", id, fullName);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a student by primary key.
 *
 * @param id  UUID of the student to remove.
 * @return Result<void> — fails with NotFound if no row was deleted.
 */
Result<void> StudentRepository::remove(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "DELETE FROM students WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[StudentRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[StudentRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Student not found: " + id
        });
    }

    spdlog::debug("[StudentRepository] removed student id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
