/**
 * @file TeacherLocationRepository.cpp
 * @brief Manage teacher-location assignments for role-based access control.
 *
 * Teachers can only access computers in their assigned locations.
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 */

#include "../core/PrecompiledHeader.hpp"
#include "TeacherLocationRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>

namespace hub32api::db {

TeacherLocationRepository::TeacherLocationRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// assign
// ---------------------------------------------------------------------------

/**
 * @brief Assigns a teacher to a location (grants access).
 *
 * Uses INSERT OR IGNORE to silently handle duplicate assignments.
 *
 * @param teacherId    UUID of the teacher.
 * @param locationId   UUID of the location.
 * @return Result containing void on success, or an error.
 */
Result<void> TeacherLocationRepository::assign(const std::string& teacherId,
                                               const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "INSERT OR IGNORE INTO teacher_locations(teacher_id, location_id) VALUES(?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherLocationRepository] assign prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, teacherId.c_str(),  static_cast<int>(teacherId.size()),  SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherLocationRepository] assign step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[TeacherLocationRepository] assigned teacher={} to location={}", teacherId, locationId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// revoke
// ---------------------------------------------------------------------------

/**
 * @brief Revokes a teacher's access to a location.
 *
 * @param teacherId    UUID of the teacher.
 * @param locationId   UUID of the location.
 * @return Result containing void on success, or an error.
 */
Result<void> TeacherLocationRepository::revoke(const std::string& teacherId,
                                               const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "DELETE FROM teacher_locations WHERE teacher_id = ? AND location_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherLocationRepository] revoke prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, teacherId.c_str(),  static_cast<int>(teacherId.size()),  SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherLocationRepository] revoke step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[TeacherLocationRepository] revoked teacher={} from location={}", teacherId, locationId);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// hasAccess
// ---------------------------------------------------------------------------

/**
 * @brief Checks if a teacher has access to a location.
 *
 * @param teacherId    UUID of the teacher.
 * @param locationId   UUID of the location.
 * @return true if the teacher has access, false otherwise.
 */
bool TeacherLocationRepository::hasAccess(const std::string& teacherId,
                                          const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT 1 FROM teacher_locations WHERE teacher_id = ? AND location_id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherLocationRepository] hasAccess prepare failed: {}", sqlite3_errmsg(m_db));
        return false;
    }

    sqlite3_bind_text(stmt, 1, teacherId.c_str(),  static_cast<int>(teacherId.size()),  SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_ROW;
}

// ---------------------------------------------------------------------------
// getLocationIdsForTeacher
// ---------------------------------------------------------------------------

/**
 * @brief Returns all location IDs that a teacher has access to.
 *
 * @param teacherId  UUID of the teacher.
 * @return Vector of location IDs (possibly empty).
 */
std::vector<std::string> TeacherLocationRepository::getLocationIdsForTeacher(
    const std::string& teacherId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT location_id FROM teacher_locations WHERE teacher_id = ? ORDER BY location_id;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherLocationRepository] getLocationIdsForTeacher prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return {};
    }

    sqlite3_bind_text(stmt, 1, teacherId.c_str(), static_cast<int>(teacherId.size()), SQLITE_STATIC);

    std::vector<std::string> locationIds;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) {
            locationIds.push_back(std::string(txt));
        }
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherLocationRepository] getLocationIdsForTeacher step failed: {}",
                      sqlite3_errmsg(m_db));
        return {};
    }

    return locationIds;
}

// ---------------------------------------------------------------------------
// getTeacherIdsForLocation
// ---------------------------------------------------------------------------

/**
 * @brief Returns all teacher IDs that have access to a location.
 *
 * @param locationId  UUID of the location.
 * @return Vector of teacher IDs (possibly empty).
 */
std::vector<std::string> TeacherLocationRepository::getTeacherIdsForLocation(
    const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "SELECT teacher_id FROM teacher_locations WHERE location_id = ? ORDER BY teacher_id;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TeacherLocationRepository] getTeacherIdsForLocation prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return {};
    }

    sqlite3_bind_text(stmt, 1, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);

    std::vector<std::string> teacherIds;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) {
            teacherIds.push_back(std::string(txt));
        }
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[TeacherLocationRepository] getTeacherIdsForLocation step failed: {}",
                      sqlite3_errmsg(m_db));
        return {};
    }

    return teacherIds;
}

} // namespace hub32api::db
