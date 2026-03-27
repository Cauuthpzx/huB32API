/**
 * @file ClassRepository.cpp
 * @brief CRUD operations for the classes table using SQLite prepared statements.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "ClassRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

ClassRepository::ClassRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new class record, generating a UUID as the primary key.
 *
 * @param tenantId   UUID of the owning tenant.
 * @param schoolId   UUID of the school this class belongs to.
 * @param name       Display name of the class.
 * @param teacherId  UUID of the assigned teacher, or empty string for no teacher.
 * @return Result containing the new class UUID on success, or an error.
 */
Result<std::string> ClassRepository::create(const std::string& tenantId,
                                              const std::string& schoolId,
                                              const std::string& name,
                                              const std::string& teacherId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[ClassRepository] UUID generation failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "INSERT INTO classes(id, tenant_id, school_id, name, teacher_id, created_at)"
        " VALUES(?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, schoolId.c_str(), static_cast<int>(schoolId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, name.c_str(),     static_cast<int>(name.size()),     SQLITE_STATIC);

    if (teacherId.empty()) {
        sqlite3_bind_null(stmt, 5);
    } else {
        sqlite3_bind_text(stmt, 5, teacherId.c_str(), static_cast<int>(teacherId.size()), SQLITE_STATIC);
    }

    sqlite3_bind_int64(stmt, 6, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ClassRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[ClassRepository] created class id={}", id);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a class by its primary key.
 *
 * @param id  UUID of the class.
 * @return Result containing the ClassRecord, or NotFound if not present.
 */
Result<ClassRecord> ClassRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, school_id, name, teacher_id, created_at"
        " FROM classes WHERE id=? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<ClassRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        ClassRecord rec;
        rec.id        = col_text(0);
        rec.tenantId  = col_text(1);
        rec.schoolId  = col_text(2);
        rec.name      = col_text(3);
        rec.teacherId = (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                        ? std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
                        : std::string{};
        rec.createdAt = sqlite3_column_int64(stmt, 5);
        sqlite3_finalize(stmt);
        return Result<ClassRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<ClassRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Class not found: " + id
        });
    }

    spdlog::error("[ClassRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<ClassRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// listByTenant
// ---------------------------------------------------------------------------

/**
 * @brief Returns all classes belonging to a tenant, ordered by name.
 *
 * @param tenantId  UUID of the tenant.
 * @return Result containing a (possibly empty) vector of ClassRecord.
 */
Result<std::vector<ClassRecord>> ClassRepository::listByTenant(const std::string& tenantId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, school_id, name, teacher_id, created_at"
        " FROM classes WHERE tenant_id=? ORDER BY name;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] listByTenant prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ClassRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, tenantId.c_str(), static_cast<int>(tenantId.size()), SQLITE_STATIC);

    std::vector<ClassRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        ClassRecord rec;
        rec.id        = col_text(0);
        rec.tenantId  = col_text(1);
        rec.schoolId  = col_text(2);
        rec.name      = col_text(3);
        rec.teacherId = (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                        ? std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
                        : std::string{};
        rec.createdAt = sqlite3_column_int64(stmt, 5);
        result.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ClassRepository] listByTenant step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ClassRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<ClassRecord>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// listByTeacher
// ---------------------------------------------------------------------------

/**
 * @brief Returns all classes assigned to a specific teacher, ordered by name.
 *
 * @param teacherId  UUID of the teacher.
 * @return Result containing a (possibly empty) vector of ClassRecord.
 */
Result<std::vector<ClassRecord>> ClassRepository::listByTeacher(const std::string& teacherId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT id, tenant_id, school_id, name, teacher_id, created_at"
        " FROM classes WHERE teacher_id=? ORDER BY name;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] listByTeacher prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ClassRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, teacherId.c_str(), static_cast<int>(teacherId.size()), SQLITE_STATIC);

    std::vector<ClassRecord> result;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        ClassRecord rec;
        rec.id        = col_text(0);
        rec.tenantId  = col_text(1);
        rec.schoolId  = col_text(2);
        rec.name      = col_text(3);
        rec.teacherId = (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
                        ? std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4)))
                        : std::string{};
        rec.createdAt = sqlite3_column_int64(stmt, 5);
        result.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ClassRepository] listByTeacher step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ClassRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<ClassRecord>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates the name and teacher assignment of an existing class.
 *
 * Returns a failure result if no row was changed (i.e. the id does not exist).
 *
 * @param id         UUID of the class to update.
 * @param name       New display name.
 * @param teacherId  New teacher UUID, or empty string to set NULL.
 */
Result<void> ClassRepository::update(const std::string& id,
                                      const std::string& name,
                                      const std::string& teacherId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE classes SET name=?, teacher_id=? WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), static_cast<int>(name.size()), SQLITE_STATIC);

    if (teacherId.empty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, teacherId.c_str(), static_cast<int>(teacherId.size()), SQLITE_STATIC);
    }

    sqlite3_bind_text(stmt, 3, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ClassRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Class not found: " + id
        });
    }

    spdlog::debug("[ClassRepository] updated class id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a class by its primary key.
 *
 * Due to ON DELETE CASCADE defined on the students table, all child
 * students are automatically removed.
 *
 * @param id  UUID of the class to delete.
 */
Result<void> ClassRepository::remove(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "DELETE FROM classes WHERE id=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ClassRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ClassRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Class not found: " + id
        });
    }

    spdlog::debug("[ClassRepository] removed class id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
