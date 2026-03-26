/**
 * @file SchoolRepository.cpp
 * @brief CRUD operations for the schools table using SQLite prepared statements.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "SchoolRepository.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

SchoolRepository::SchoolRepository(sqlite3* db)
    : m_db(db)
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new school record, generating a UUID as the primary key.
 *
 * @param name     Display name of the school.
 * @param address  Street address of the school.
 * @return Result containing the new school UUID on success, or an error.
 */
Result<std::string> SchoolRepository::create(const std::string& name, const std::string& address)
{
    std::string id;
    try {
        id = CryptoUtils::generateUuid();
    } catch (const std::exception& ex) {
        spdlog::error("[SchoolRepository] UUID generation failed: {}", ex.what());
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "INSERT INTO schools(id, name, address, created_at) VALUES(?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SchoolRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),      static_cast<int>(id.size()),      SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name.c_str(),    static_cast<int>(name.size()),    SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, address.c_str(), static_cast<int>(address.size()), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, now);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[SchoolRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[SchoolRepository] created school id={}", id);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a school by its primary key.
 *
 * @param id  UUID of the school.
 * @return Result containing the SchoolRecord, or NotFound if not present.
 */
Result<SchoolRecord> SchoolRepository::findById(const std::string& id)
{
    constexpr const char* k_sql =
        "SELECT id, name, address, created_at FROM schools WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SchoolRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<SchoolRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        SchoolRecord rec;
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        rec.id        = col_text(0);
        rec.name      = col_text(1);
        rec.address   = col_text(2);
        rec.createdAt = sqlite3_column_int64(stmt, 3);
        sqlite3_finalize(stmt);
        return Result<SchoolRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<SchoolRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "School not found: " + id
        });
    }

    spdlog::error("[SchoolRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<SchoolRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// listAll
// ---------------------------------------------------------------------------

/**
 * @brief Returns all school records ordered by created_at.
 *
 * @return Result containing a (possibly empty) vector of SchoolRecord.
 */
Result<std::vector<SchoolRecord>> SchoolRepository::listAll()
{
    constexpr const char* k_sql =
        "SELECT id, name, address, created_at FROM schools ORDER BY created_at ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SchoolRepository] listAll prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<SchoolRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    std::vector<SchoolRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        SchoolRecord rec;
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        rec.id        = col_text(0);
        rec.name      = col_text(1);
        rec.address   = col_text(2);
        rec.createdAt = sqlite3_column_int64(stmt, 3);
        records.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[SchoolRepository] listAll step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<SchoolRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<SchoolRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates the name and address of an existing school.
 *
 * Returns a failure result if no row was changed (i.e. the id does not exist).
 *
 * @param id       UUID of the school to update.
 * @param name     New display name.
 * @param address  New address.
 */
Result<void> SchoolRepository::update(const std::string& id,
                                       const std::string& name,
                                       const std::string& address)
{
    constexpr const char* k_sql =
        "UPDATE schools SET name = ?, address = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SchoolRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, name.c_str(),    static_cast<int>(name.size()),    SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, address.c_str(), static_cast<int>(address.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, id.c_str(),      static_cast<int>(id.size()),      SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[SchoolRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "School not found: " + id
        });
    }

    spdlog::debug("[SchoolRepository] updated school id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a school by its primary key.
 *
 * Due to ON DELETE CASCADE defined on the locations table, all child
 * locations are automatically removed.
 *
 * @param id  UUID of the school to delete.
 */
Result<void> SchoolRepository::remove(const std::string& id)
{
    constexpr const char* k_sql =
        "DELETE FROM schools WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[SchoolRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[SchoolRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[SchoolRepository] removed school id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
