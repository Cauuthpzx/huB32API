/**
 * @file ComputerRepository.cpp
 * @brief CRUD operations for the computers table using SQLite prepared statements.
 *
 * All SQL uses sqlite3_prepare_v2 / sqlite3_bind_* / sqlite3_step / sqlite3_finalize.
 * No string concatenation is used for SQL query construction.
 * location_id is a nullable foreign key — an empty locationId string is stored as NULL.
 */

#include "../core/PrecompiledHeader.hpp"
#include "ComputerRepository.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include "core/internal/CryptoUtils.hpp"

namespace hub32api::db {

using hub32api::core::internal::CryptoUtils;

// ---------------------------------------------------------------------------
// Helper: populate a ComputerRecord from the current statement row.
// Column order must match all SELECT statements in this file:
//   0:id, 1:location_id, 2:hostname, 3:mac_address,
//   4:ip_last_seen, 5:agent_version, 6:last_heartbeat,
//   7:state, 8:position_x, 9:position_y
// ---------------------------------------------------------------------------
static ComputerRecord recordFromStmt(sqlite3_stmt* stmt)
{
    auto col_text = [&](int col) -> std::string {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return txt ? std::string(txt) : std::string{};
    };

    ComputerRecord rec;
    rec.id            = col_text(0);
    rec.locationId    = col_text(1);   // empty when NULL
    rec.hostname      = col_text(2);
    rec.macAddress    = col_text(3);
    rec.ipLastSeen    = col_text(4);
    rec.agentVersion  = col_text(5);
    rec.lastHeartbeat = sqlite3_column_int64(stmt, 6);
    rec.state         = col_text(7);
    rec.positionX     = sqlite3_column_int(stmt, 8);
    rec.positionY     = sqlite3_column_int(stmt, 9);
    return rec;
}

constexpr const char* k_selectCols =
    "SELECT id, location_id, hostname, mac_address, "
    "ip_last_seen, agent_version, last_heartbeat, state, position_x, position_y ";

ComputerRepository::ComputerRepository(DatabaseManager& dbManager)
    : m_dbManager(dbManager)
    , m_db(dbManager.schoolDb())
{
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

/**
 * @brief Inserts a new computer record, generating a UUID as the primary key.
 *
 * @param locationId  UUID of the owning location, or empty string for unassigned.
 * @param hostname    Network hostname of the computer.
 * @param macAddress  MAC address string.
 * @return Result containing the new computer UUID on success, or an error.
 */
Result<std::string> ComputerRepository::create(const std::string& locationId,
                                                const std::string& hostname,
                                                const std::string& macAddress)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    auto uuidResult = CryptoUtils::generateUuid();
    if (uuidResult.is_err()) {
        spdlog::error("[ComputerRepository] UUID generation failed: {}", uuidResult.error().message);
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            "UUID generation failed"
        });
    }
    const std::string id = uuidResult.take();

    constexpr const char* k_sql =
        "INSERT INTO computers(id, location_id, hostname, mac_address, "
        "ip_last_seen, agent_version, last_heartbeat, state, position_x, position_y) "
        "VALUES(?, ?, ?, ?, '', '', 0, 'offline', 0, 0);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] create prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    // Bind location_id as NULL when empty, otherwise as text
    if (locationId.empty()) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);
    }

    sqlite3_bind_text(stmt, 3, hostname.c_str(),   static_cast<int>(hostname.size()),   SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, macAddress.c_str(), static_cast<int>(macAddress.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] create step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::string>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[ComputerRepository] created computer id={}", id);
    return Result<std::string>::ok(std::move(id));
}

// ---------------------------------------------------------------------------
// createUnassigned
// ---------------------------------------------------------------------------

/**
 * @brief Convenience wrapper for agents registering without a known location.
 *
 * Calls create() with an empty locationId which results in NULL in the DB.
 */
Result<std::string> ComputerRepository::createUnassigned(const std::string& hostname,
                                                          const std::string& macAddress)
{
    return create("", hostname, macAddress);
}

// ---------------------------------------------------------------------------
// findById
// ---------------------------------------------------------------------------

/**
 * @brief Finds a computer by its primary key.
 *
 * @param id  UUID of the computer.
 * @return Result containing the ComputerRecord, or NotFound if absent.
 */
Result<ComputerRecord> ComputerRepository::findById(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = std::string(k_selectCols) +
        "FROM computers WHERE id = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] findById prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        ComputerRecord rec = recordFromStmt(stmt);
        sqlite3_finalize(stmt);
        return Result<ComputerRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found: " + id
        });
    }

    spdlog::error("[ComputerRepository] findById step failed: {}", sqlite3_errmsg(m_db));
    return Result<ComputerRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// findByHostname
// ---------------------------------------------------------------------------

/**
 * @brief Finds a computer by its hostname.
 *
 * @param hostname  Exact hostname to look up.
 * @return Result containing the ComputerRecord, or NotFound if absent.
 */
Result<ComputerRecord> ComputerRepository::findByHostname(const std::string& hostname)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = std::string(k_selectCols) +
        "FROM computers WHERE hostname = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] findByHostname prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, hostname.c_str(), static_cast<int>(hostname.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        ComputerRecord rec = recordFromStmt(stmt);
        sqlite3_finalize(stmt);
        return Result<ComputerRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found with hostname: " + hostname
        });
    }

    spdlog::error("[ComputerRepository] findByHostname step failed: {}", sqlite3_errmsg(m_db));
    return Result<ComputerRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// findByMac
// ---------------------------------------------------------------------------

/**
 * @brief Finds a computer by its MAC address.
 *
 * @param mac  Exact MAC address string to look up.
 * @return Result containing the ComputerRecord, or NotFound if absent.
 */
Result<ComputerRecord> ComputerRepository::findByMac(const std::string& mac)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = std::string(k_selectCols) +
        "FROM computers WHERE mac_address = ? LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] findByMac prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, mac.c_str(), static_cast<int>(mac.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        ComputerRecord rec = recordFromStmt(stmt);
        sqlite3_finalize(stmt);
        return Result<ComputerRecord>::ok(std::move(rec));
    }

    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return Result<ComputerRecord>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found with MAC: " + mac
        });
    }

    spdlog::error("[ComputerRepository] findByMac step failed: {}", sqlite3_errmsg(m_db));
    return Result<ComputerRecord>::fail(ApiError{
        ErrorCode::InternalError,
        sqlite3_errmsg(m_db)
    });
}

// ---------------------------------------------------------------------------
// listByLocation
// ---------------------------------------------------------------------------

/**
 * @brief Returns all computers assigned to the given location.
 *
 * @param locationId  UUID of the location.
 * @return Result containing a (possibly empty) vector of ComputerRecord.
 */
Result<std::vector<ComputerRecord>> ComputerRepository::listByLocation(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = std::string(k_selectCols) +
        "FROM computers WHERE location_id = ? ORDER BY hostname ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] listByLocation prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ComputerRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);

    std::vector<ComputerRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        records.push_back(recordFromStmt(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] listByLocation step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ComputerRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<ComputerRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// listAll
// ---------------------------------------------------------------------------

/**
 * @brief Returns all computer records ordered by hostname.
 *
 * @return Result containing a (possibly empty) vector of ComputerRecord.
 */
Result<std::vector<ComputerRecord>> ComputerRepository::listAll()
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const std::string k_sql = std::string(k_selectCols) +
        "FROM computers ORDER BY hostname ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] listAll prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ComputerRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    std::vector<ComputerRecord> records;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        records.push_back(recordFromStmt(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] listAll step failed: {}", sqlite3_errmsg(m_db));
        return Result<std::vector<ComputerRecord>>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    return Result<std::vector<ComputerRecord>>::ok(std::move(records));
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

/**
 * @brief Updates location, hostname, and position for an existing computer.
 *
 * @param id          UUID of the computer.
 * @param locationId  New location UUID, or empty string to set NULL.
 * @param hostname    New hostname.
 * @param posX        New X position.
 * @param posY        New Y position.
 */
Result<void> ComputerRepository::update(const std::string& id,
                                         const std::string& locationId,
                                         const std::string& hostname,
                                         int posX,
                                         int posY)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE computers SET location_id = ?, hostname = ?, position_x = ?, position_y = ? "
        "WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] update prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (locationId.empty()) {
        sqlite3_bind_null(stmt, 1);
    } else {
        sqlite3_bind_text(stmt, 1, locationId.c_str(), static_cast<int>(locationId.size()), SQLITE_STATIC);
    }
    sqlite3_bind_text(stmt, 2, hostname.c_str(), static_cast<int>(hostname.size()), SQLITE_STATIC);
    sqlite3_bind_int(stmt,  3, posX);
    sqlite3_bind_int(stmt,  4, posY);
    sqlite3_bind_text(stmt, 5, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] update step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found: " + id
        });
    }

    spdlog::debug("[ComputerRepository] updated computer id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// updateState
// ---------------------------------------------------------------------------

/**
 * @brief Updates the state field for a computer.
 *
 * @param id     UUID of the computer.
 * @param state  New state string: "offline", "online", "locked", or "demo".
 */
Result<void> ComputerRepository::updateState(const std::string& id, const std::string& state)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql =
        "UPDATE computers SET state = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] updateState prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, state.c_str(), static_cast<int>(state.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, id.c_str(),    static_cast<int>(id.size()),    SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] updateState step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found: " + id
        });
    }

    spdlog::debug("[ComputerRepository] updated state for computer id={} -> {}", id, state);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// updateHeartbeat
// ---------------------------------------------------------------------------

/**
 * @brief Records a heartbeat: sets last_heartbeat to now, updates ip and agent version,
 *        and transitions state to "online".
 *
 * @param id            UUID of the computer.
 * @param ip            Current IP address as seen by the server.
 * @param agentVersion  Version string reported by the agent.
 */
Result<void> ComputerRepository::updateHeartbeat(const std::string& id,
                                                  const std::string& ip,
                                                  const std::string& agentVersion)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    constexpr const char* k_sql =
        "UPDATE computers SET last_heartbeat = ?, ip_last_seen = ?, "
        "agent_version = ?, state = 'online' WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] updateHeartbeat prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_text(stmt,  2, ip.c_str(),           static_cast<int>(ip.size()),           SQLITE_STATIC);
    sqlite3_bind_text(stmt,  3, agentVersion.c_str(), static_cast<int>(agentVersion.size()), SQLITE_STATIC);
    sqlite3_bind_text(stmt,  4, id.c_str(),           static_cast<int>(id.size()),           SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] updateHeartbeat step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    if (sqlite3_changes(m_db) == 0) {
        return Result<void>::fail(ApiError{
            ErrorCode::NotFound,
            "Computer not found: " + id
        });
    }

    spdlog::debug("[ComputerRepository] heartbeat updated for computer id={}", id);
    return Result<void>::ok();
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

/**
 * @brief Deletes a computer by its primary key.
 *
 * @param id  UUID of the computer to delete.
 */
Result<void> ComputerRepository::remove(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_dbManager.dbMutex());

    constexpr const char* k_sql = "DELETE FROM computers WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[ComputerRepository] remove prepare failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    sqlite3_bind_text(stmt, 1, id.c_str(), static_cast<int>(id.size()), SQLITE_STATIC);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        spdlog::error("[ComputerRepository] remove step failed: {}", sqlite3_errmsg(m_db));
        return Result<void>::fail(ApiError{
            ErrorCode::InternalError,
            sqlite3_errmsg(m_db)
        });
    }

    spdlog::debug("[ComputerRepository] removed computer id={}", id);
    return Result<void>::ok();
}

} // namespace hub32api::db
