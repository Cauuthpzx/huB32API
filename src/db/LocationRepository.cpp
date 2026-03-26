/**
 * @file LocationRepository.cpp
 * @brief CRUD operations for the locations table using SQLite prepared statements.
 *
 * Locations belong to a school via a foreign key (school_id). All SQL uses
 * sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "LocationRepository.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

LocationRepository::LocationRepository(sqlite3* db)
    : m_db(db)
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new location record, generating a UUID as the primary key.
 *
 * @param schoolId  UUID of the parent school.
 * @param name      Display name of the location.
 * @param building  Building identifier.
 * @param floor     Floor number.
 * @param capacity  Seat/station capacity.
 * @param type      One of "classroom", "lab", "office" (default: "classroom").
 * @return Result containing the new location UUID on success, or an error.
 */
Result<std::string> LocationRepository::create(const std::string& schoolId,
                                                const std::string& name,
                                                const std::string& building,
                                                int floor,
                                                int capacity,
                                                const std::string& type)
{
    std::string id;
    try {
        id = CryptoUtils::generateUuid();
    } catch (const std::exception& ex) {
        spdlog::error("[LocationRepository] UUID generation failed: {}", ex.what());
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }

    constexpr const char* k_sql =
        "INSERT INTO locations(id, school_id, name, building, floor, capacity, type) "
        "VALUES(?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, schoolId.c_str(), static_cast<int>(schoolId.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, name.c_str(),     static_cast<int>(name.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, building.c_str(), static_cast<int>(building.size()), SQLITE_STATIC);
    sqlite3_bind_int(stmt,  5, floor);
    sqlite3_bind_int(stmt,  6, capacity);
    sqlite3_bind_text(stmt, 7, type.c_str(),     static_cast<int>(type.size()),     SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[LocationRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[LocationRepository] created location id={}", id);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a location by its primary key.
 *
 * @param id  UUID of the location.
 * @return Result containing the LocationRecord, or NotFound if not present.
 */
Result<LocationRecord> LocationRepository::findById(const std::string& id)
{
    constexpr const char* k_sql =
        "SELECT id, school_id, name, building, floor, capacity, type "
        "FROM locations WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<LocationRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        LocationRecord rec;
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        rec.id       = col_text(0);
        rec.schoolId = col_text(1);
        rec.name     = col_text(2);
        rec.building = col_text(3);
        rec.floor    = sqlite3_column_int(stmt, 4);
        rec.capacity = sqlite3_column_int(stmt, 5);
        rec.type     = col_text(6);
        sqlite3_finalize(stmt);
        return Result<LocationRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<LocationRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Location not found: " + id
        });
    }

    spdlog::error("[LocationRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<LocationRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// listBySchool
// ---------------------------------------------------------------------------

/**
 * @brief Returns all locations belonging to a given school.
 *
 * @param schoolId  UUID of the parent school.
 * @return Result containing a (possibly empty) vector of LocationRecord.
 */
Result<std::vector<LocationRecord>> LocationRepository::listBySchool(const std::string& schoolId)
{
    constexpr const char* k_sql =
        "SELECT id, school_id, name, building, floor, capacity, type "
        "FROM locations WHERE school_id = ? ORDER BY name ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] listBySchool prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<LocationRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, schoolId.c_str(), static_cast<int>(schoolId.size()), SQLITE_STATIC);

    std::vector<LocationRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        LocationRecord rec;
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        rec.id       = col_text(0);
        rec.schoolId = col_text(1);
        rec.name     = col_text(2);
        rec.building = col_text(3);
        rec.floor    = sqlite3_column_int(stmt, 4);
        rec.capacity = sqlite3_column_int(stmt, 5);
        rec.type     = col_text(6);
        records.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[LocationRepository] listBySchool step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<LocationRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<LocationRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// listAll
// ---------------------------------------------------------------------------

/**
 * @brief Returns all location records across all schools.
 *
 * @return Result containing a (possibly empty) vector of LocationRecord.
 */
Result<std::vector<LocationRecord>> LocationRepository::listAll()
{
    constexpr const char* k_sql =
        "SELECT id, school_id, name, building, floor, capacity, type "
        "FROM locations ORDER BY school_id ASC, name ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] listAll prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<LocationRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    std::vector<LocationRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        LocationRecord rec;
        auto col_text = [&](int col) -> std::string {
            const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return txt ? std::string(txt) : std::string{};
        };
        rec.id       = col_text(0);
        rec.schoolId = col_text(1);
        rec.name     = col_text(2);
        rec.building = col_text(3);
        rec.floor    = sqlite3_column_int(stmt, 4);
        rec.capacity = sqlite3_column_int(stmt, 5);
        rec.type     = col_text(6);
        records.push_back(std::move(rec));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[LocationRepository] listAll step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<LocationRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<LocationRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates name, building, floor, capacity, and type for a location.
 *
 * Returns a failure result if no row was changed (i.e. the id does not exist).
 *
 * @param id        UUID of the location to update.
 * @param name      New display name.
 * @param building  New building identifier.
 * @param floor     New floor number.
 * @param capacity  New seat/station capacity.
 * @param type      New type string.
 */
Result<void> LocationRepository::update(const std::string& id,
                                         const std::string& name,
                                         const std::string& building,
                                         int floor,
                                         int capacity,
                                         const std::string& type)
{
    constexpr const char* k_sql =
        "UPDATE locations SET name = ?, building = ?, floor = ?, capacity = ?, type = ? "
        "WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, name.c_str(),     static_cast<int>(name.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, building.c_str(), static_cast<int>(building.size()), SQLITE_STATIC);
    sqlite3_bind_int(stmt,  3, floor);
    sqlite3_bind_int(stmt,  4, capacity);
    sqlite3_bind_text(stmt, 5, type.c_str(),     static_cast<int>(type.size()),     SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, id.c_str(),       static_cast<int>(id.size()),       SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[LocationRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Location not found: " + id
        });
    }

    spdlog::debug("[LocationRepository] updated location id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a location by its primary key.
 *
 * @param id  UUID of the location to delete.
 */
Result<void> LocationRepository::remove(const std::string& id)
{
    constexpr const char* k_sql =
        "DELETE FROM locations WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[LocationRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[LocationRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[LocationRepository] removed location id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
