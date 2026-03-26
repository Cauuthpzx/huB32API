/**
 * @file DatabaseManager.cpp
 * @brief Implementation of DatabaseManager — opens school.db and creates
 *        the full HUB32 schema with WAL mode and foreign key enforcement.
 *
 * Tables: schools, locations, computers, teachers, teacher_locations,
 *         active_sessions.
 * All timestamps are stored as Unix epoch seconds (INTEGER).
 */

#include "../core/PrecompiledHeader.hpp"
#include "DatabaseManager.hpp"

#include <sqlite3.h>
#include <filesystem>

namespace hub32api::db {

namespace {

constexpr const char* k_schemaSql = R"(
CREATE TABLE IF NOT EXISTS schools (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    address TEXT,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS locations (
    id TEXT PRIMARY KEY,
    school_id TEXT NOT NULL REFERENCES schools(id) ON DELETE CASCADE,
    name TEXT NOT NULL,
    building TEXT,
    floor INTEGER,
    capacity INTEGER,
    type TEXT DEFAULT 'classroom'
);

CREATE TABLE IF NOT EXISTS computers (
    id TEXT PRIMARY KEY,
    location_id TEXT REFERENCES locations(id) ON DELETE SET NULL,
    hostname TEXT NOT NULL,
    mac_address TEXT,
    ip_last_seen TEXT,
    agent_version TEXT,
    last_heartbeat INTEGER,
    state TEXT DEFAULT 'offline',
    position_x INTEGER,
    position_y INTEGER
);

CREATE TABLE IF NOT EXISTS teachers (
    id TEXT PRIMARY KEY,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    full_name TEXT NOT NULL,
    role TEXT DEFAULT 'teacher',
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS teacher_locations (
    teacher_id TEXT NOT NULL REFERENCES teachers(id) ON DELETE CASCADE,
    location_id TEXT NOT NULL REFERENCES locations(id) ON DELETE CASCADE,
    PRIMARY KEY (teacher_id, location_id)
);

CREATE TABLE IF NOT EXISTS active_sessions (
    computer_id TEXT PRIMARY KEY REFERENCES computers(id),
    user_login TEXT,
    user_fullname TEXT,
    session_start INTEGER,
    producer_id TEXT,
    transport_id TEXT
);

CREATE INDEX IF NOT EXISTS idx_computers_location ON computers(location_id);
CREATE INDEX IF NOT EXISTS idx_computers_state ON computers(state);
CREATE INDEX IF NOT EXISTS idx_locations_school ON locations(school_id);
CREATE INDEX IF NOT EXISTS idx_teachers_username ON teachers(username);
)";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Pimpl struct
// ---------------------------------------------------------------------------

struct DatabaseManager::Impl
{
    sqlite3*    db = nullptr;
    std::string dataDir;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

/**
 * @brief Opens (or creates) school.db inside @p dataDir.
 *
 * Creates the directory if it does not exist, enables WAL mode,
 * synchronous=NORMAL, foreign_keys=ON, then calls createSchema().
 *
 * @param dataDir  Directory that will hold school.db.
 */
DatabaseManager::DatabaseManager(const std::string& dataDir)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->dataDir = dataDir;

    // Ensure the data directory exists
    std::error_code ec;
    std::filesystem::create_directories(dataDir, ec);
    if (ec) {
        spdlog::error("[DatabaseManager] failed to create data directory '{}': {}",
                      dataDir, ec.message());
        return;
    }

    const std::string dbPath = dataDir + "/school.db";
    int rc = sqlite3_open(dbPath.c_str(), &m_impl->db);
    if (rc != SQLITE_OK) {
        spdlog::error("[DatabaseManager] sqlite3_open('{}') failed: {}",
                      dbPath, sqlite3_errmsg(m_impl->db));
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
        return;
    }

    // Performance and durability settings
    sqlite3_exec(m_impl->db, "PRAGMA journal_mode=WAL;",      nullptr, nullptr, nullptr);
    sqlite3_exec(m_impl->db, "PRAGMA synchronous=NORMAL;",    nullptr, nullptr, nullptr);
    sqlite3_exec(m_impl->db, "PRAGMA foreign_keys=ON;",       nullptr, nullptr, nullptr);

    createSchema();

    spdlog::info("[DatabaseManager] opened school.db at '{}'", dbPath);
}

/**
 * @brief Closes the SQLite handle.
 */
DatabaseManager::~DatabaseManager()
{
    if (m_impl->db) {
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

/**
 * @brief Returns true if the database was opened successfully.
 */
bool DatabaseManager::isOpen() const noexcept
{
    return m_impl->db != nullptr;
}

/**
 * @brief Returns the raw sqlite3* handle for school.db.
 *
 * May return nullptr if the database failed to open.
 */
sqlite3* DatabaseManager::schoolDb() noexcept
{
    return m_impl->db;
}

// ---------------------------------------------------------------------------
// Private: schema creation
// ---------------------------------------------------------------------------

/**
 * @brief Executes the full DDL for all HUB32 tables and indexes.
 *
 * Uses a single sqlite3_exec() call with the full schema SQL so that
 * each CREATE TABLE/INDEX IF NOT EXISTS is idempotent on subsequent opens.
 */
void DatabaseManager::createSchema()
{
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_impl->db, k_schemaSql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[DatabaseManager] failed to create schema: {}",
                      errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
    } else {
        spdlog::debug("[DatabaseManager] schema created/verified successfully");
    }
}

} // namespace hub32api::db
