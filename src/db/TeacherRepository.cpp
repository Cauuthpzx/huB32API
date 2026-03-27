/**
 * @file TeacherRepository.cpp
 * @brief CRUD + authentication operations for the teachers table using SQLite prepared statements.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 * Password hashing/verification delegates to UserRoleStore static methods (PBKDF2-HMAC-SHA256).
 * The password_hash column is never returned to callers — TeacherRecord deliberately omits it.
 */

#include "../core/PrecompiledHeader.hpp"
#include "TeacherRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"
#include "auth/UserRoleStore.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;
using hub32api::auth::UserRoleStore;

// ---------------------------------------------------------------------------
// Helper: populate a TeacherRecord from the current statement row.
// Column order: 0:id, 1:username, 2:full_name, 3:role, 4:created_at
// ---------------------------------------------------------------------------
static TeacherRecord recordFromStmt(sqlite3_stmt* stmt)
{
    auto col_text = [&](int col) -> std::string {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return txt ? std::string(txt) : std::string{};
    };

    TeacherRecord rec;
    rec.id        = col_text(0);
    rec.username  = col_text(1);
    rec.fullName  = col_text(2);
    rec.role      = col_text(3);
    rec.createdAt = sqlite3_column_int64(stmt, 4);
    return rec;
}

TeacherRepository::TeacherRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new teacher record with a PBKDF2-hashed password.
 *
 * @param username  Unique login name.
 * @param password  Plaintext password (stored as PBKDF2 hash).
 * @param fullName  Display name.
 * @param role      "teacher" (default) or "admin".
 * @return Result containing the new teacher UUID on success, or an error.
 */
Result<std::string> TeacherRepository::create(const std::string& username,
                                               const std::string& password,
                                               const std::string& fullName,
                                               const std::string& role)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[TeacherRepository] UUID generation failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    auto hashResult = UserRoleStore::hashPassword(password);
    if (hashResult.is_err()) {
        spdlog::error("[TeacherRepository] password hashing failed: {}", hashResult.error().message);
        return Result<std::string>::fail(hashResult.error());
    }
    const std::string passwordHash = hashResult.take();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "INSERT INTO teachers(id, username, password_hash, full_name, role, created_at) "
        "VALUES(?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),           static_cast<int>(id.size()),           SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, username.c_str(),     static_cast<int>(username.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, passwordHash.c_str(), static_cast<int>(passwordHash.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, fullName.c_str(),     static_cast<int>(fullName.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, role.c_str(),         static_cast<int>(role.size()),         SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 6, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[TeacherRepository] created teacher id={}", id);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a teacher by primary key. Does NOT return password_hash.
 *
 * @param id  UUID of the teacher.
 * @return Result containing TeacherRecord, or NotFound.
 */
Result<TeacherRecord> TeacherRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, username, full_name, role, created_at "
        "FROM teachers WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        TeacherRecord rec = recordFromStmt(stmt);
        sqlite3_finalize(stmt);
        return Result<TeacherRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Teacher not found: " + id
        });
    }

    spdlog::error("[TeacherRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<TeacherRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// findByUsername
// ---------------------------------------------------------------------------

/**
 * @brief Finds a teacher by their unique username. Does NOT return password_hash.
 *
 * @param username  Login name to look up.
 * @return Result containing TeacherRecord, or NotFound.
 */
Result<TeacherRecord> TeacherRepository::findByUsername(const std::string& username)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, username, full_name, role, created_at "
        "FROM teachers WHERE username = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] findByUsername prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), static_cast<int>(username.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        TeacherRecord rec = recordFromStmt(stmt);
        sqlite3_finalize(stmt);
        return Result<TeacherRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Teacher not found: " + username
        });
    }

    spdlog::error("[TeacherRepository] findByUsername step failed: {}", sqlite3_errmsg(m_db));
    return Result<TeacherRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// authenticate
// ---------------------------------------------------------------------------

/**
 * @brief Verifies username/password and returns the full TeacherRecord on success.
 *
 * When @p tenantId is non-empty, the SQL adds AND tenant_id=? so only teachers
 * belonging to that specific tenant are matched (multi-tenant login path).
 * When @p tenantId is empty, any teacher matching the username is checked
 * (backwards-compat fallback for non-tenant auth paths).
 *
 * SECURITY: The same error message is returned whether the user is not found
 * or the password is wrong, to prevent username enumeration attacks.
 *
 * @param username  Login name.
 * @param password  Plaintext password to verify.
 * @param tenantId  Optional tenant scope. Empty = no tenant filter.
 * @return Result containing TeacherRecord on success, or AuthenticationFailed.
 */
Result<TeacherRecord> TeacherRepository::authenticate(const std::string& username,
                                                       const std::string& password,
                                                       const std::string& tenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    // Build SQL dynamically — no string concatenation for values, only for structure.
    // When tenantId is supplied, scope the lookup to that tenant.
    const std::string k_sql = tenantId.empty()
        ? "SELECT id, username, full_name, role, created_at, password_hash "
          "FROM teachers WHERE username = ? LIMIT 1;"
        : "SELECT id, username, full_name, role, created_at, password_hash "
          "FROM teachers WHERE username = ? AND tenant_id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] authenticate prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), static_cast<int>(username.size()), SQLITE_STATIC);
    if (!tenantId.empty()) {
        sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    }

    const int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        // Columns: 0:id, 1:username, 2:full_name, 3:role, 4:created_at, 5:password_hash
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };

        TeacherRecord rec;
        rec.id        = col_text(0);
        rec.username  = col_text(1);
        rec.fullName  = col_text(2);
        rec.role      = col_text(3);
        rec.createdAt = sqlite3_column_int64(stmt, 4);
        const std::string storedHash = col_text(5);
        sqlite3_finalize(stmt);

        if (UserRoleStore::verifyPassword(password, storedHash)) {
            spdlog::debug("[TeacherRepository] authenticate success for username={} tenantId={}",
                          username, tenantId);
            return Result<TeacherRecord>::ok(std::move(rec));
        }

        // Wrong password — same message as user-not-found to prevent enumeration
        return Result<TeacherRecord>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid username or password"
        });
    }

    sqlite3_finalize(stmt);

    // User not found — same message as wrong password
    return Result<TeacherRecord>::fail(ApiError{
        ErrorCode::AuthenticationFailed,
        "Invalid username or password"
    });
}

// ---------------------------------------------------------------------------
// listAll
// ---------------------------------------------------------------------------

/**
 * @brief Returns teacher records ordered by username. Does NOT return password_hash.
 *
 * When @p tenantId is non-empty, filters to teachers belonging to that tenant
 * (WHERE tenant_id = ?). When empty (superadmin path), returns all teachers.
 *
 * @param tenantId  Optional tenant scope. Empty = no filter (superadmin).
 * @return Result containing a (possibly empty) vector of TeacherRecord.
 */
Result<std::vector<TeacherRecord>> TeacherRepository::listAll(const std::string& tenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = tenantId.empty()
        ? "SELECT id, username, full_name, role, created_at "
          "FROM teachers ORDER BY username ASC;"
        : "SELECT id, username, full_name, role, created_at "
          "FROM teachers WHERE tenant_id = ? ORDER BY username ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] listAll prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<TeacherRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (!tenantId.empty()) {
        sqlite3_bind_text(stmt, 1, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    }

    std::vector<TeacherRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        records.push_back(recordFromStmt(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] listAll step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<TeacherRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<TeacherRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates full_name and role for an existing teacher.
 *
 * @param id        UUID of the teacher.
 * @param fullName  New display name.
 * @param role      New role ("teacher" or "admin").
 */
Result<void> TeacherRepository::update(const std::string& id,
                                        const std::string& fullName,
                                        const std::string& role)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE teachers SET full_name = ?, role = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, fullName.c_str(), static_cast<int>(fullName.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, role.c_str(),     static_cast<int>(role.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Teacher not found: " + id
        });
    }

    spdlog::debug("[TeacherRepository] updated teacher id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// changePassword
// ---------------------------------------------------------------------------

/**
 * @brief Replaces the password hash for an existing teacher.
 *
 * @param id           UUID of the teacher.
 * @param newPassword  Plaintext new password (will be hashed before storing).
 */
Result<void> TeacherRepository::changePassword(const std::string& id,
                                                const std::string& newPassword)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto hashResult = UserRoleStore::hashPassword(newPassword);
    if (hashResult.is_err()) {
        spdlog::error("[TeacherRepository] password hashing failed: {}", hashResult.error().message);
        return Result<void>::fail(hashResult.error());
    }
    const std::string newHash = hashResult.take();

    constexpr const char* k_sql =
        "UPDATE teachers SET password_hash = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] changePassword prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, newHash.c_str(), static_cast<int>(newHash.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(),      static_cast<int>(id.size()),      SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] changePassword step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Teacher not found: " + id
        });
    }

    spdlog::debug("[TeacherRepository] changed password for teacher id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// applyPasswordHash
// ---------------------------------------------------------------------------

/**
 * @brief Writes a pre-computed argon2id hash directly into password_hash.
 *
 * Called by RequestController when a pending change_password ticket is accepted.
 * The hash was computed at ticket-submission time and stored in pending_requests.payload.
 */
Result<void> TeacherRepository::applyPasswordHash(const std::string& id,
                                                   const std::string& passwordHash)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE teachers SET password_hash = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] applyPasswordHash prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, passwordHash.c_str(),
                      static_cast<int>(passwordHash.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(),
                      static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] applyPasswordHash step failed: {}",
                      sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError, sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound, "Teacher not found: " + id
        });
    }

    spdlog::debug("[TeacherRepository] applied password hash for teacher id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a teacher by primary key.
 *
 * Due to ON DELETE CASCADE on teacher_locations, all location assignments
 * are automatically removed.
 *
 * @param id  UUID of the teacher to delete.
 */
Result<void> TeacherRepository::remove(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql = "DELETE FROM teachers WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[TeacherRepository] removed teacher id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
