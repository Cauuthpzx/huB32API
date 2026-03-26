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

TeacherRepository::TeacherRepository(sqlite3* db)
    : m_db(db)
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
    std::string id;
    try {
        id = CryptoUtils::generateUuid();
    } catch (const std::exception& ex) {
        spdlog::error("[TeacherRepository] UUID generation failed: {}", ex.what());
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }

    const std::string passwordHash = UserRoleStore::hashPassword(password);

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
 * @brief Verifies username/password and returns the teacher's role on success.
 *
 * SECURITY: The same error message is returned whether the user is not found
 * or the password is wrong, to prevent username enumeration attacks.
 *
 * @param username  Login name.
 * @param password  Plaintext password to verify.
 * @return Result containing the role string on success, or AuthenticationFailed.
 */
Result<std::string> TeacherRepository::authenticate(const std::string& username,
                                                      const std::string& password)
{
    constexpr const char* k_sql =
        "SELECT password_hash, role FROM teachers WHERE username = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] authenticate prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), static_cast<int>(username.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        const std::string storedHash = col_text(0);
        const std::string role       = col_text(1);
        sqlite3_finalize(stmt);

        if (UserRoleStore::verifyPassword(password, storedHash)) {
            spdlog::debug("[TeacherRepository] authenticate success for username={}", username);
            return Result<std::string>::ok(role);
        }

        // Wrong password — same message as user-not-found to prevent enumeration
        return Result<std::string>::fail(ApiError{
            ErrorCode::AuthenticationFailed,
            "Invalid username or password"
        });
    }

    sqlite3_finalize(stmt);

    // User not found — same message as wrong password
    return Result<std::string>::fail(ApiError{
        ErrorCode::AuthenticationFailed,
        "Invalid username or password"
    });
}

// ---------------------------------------------------------------------------
// listAll
// ---------------------------------------------------------------------------

/**
 * @brief Returns all teacher records ordered by username. Does NOT return password_hash.
 *
 * @return Result containing a (possibly empty) vector of TeacherRecord.
 */
Result<std::vector<TeacherRecord>> TeacherRepository::listAll()
{
    constexpr const char* k_sql =
        "SELECT id, username, full_name, role, created_at "
        "FROM teachers ORDER BY username ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherRepository] listAll prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<TeacherRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
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
    const std::string newHash = UserRoleStore::hashPassword(newPassword);

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
