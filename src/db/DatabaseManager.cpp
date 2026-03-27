/**
 * @file DatabaseManager.cpp
 * @brief Implementation of DatabaseManager — opens school.db and creates
 *        the full HUB32 schema with WAL mode and foreign key enforcement.
 *
 * Tables: schools, locations, computers, teachers, teacher_locations,
 *         active_sessions, tenants, classes, students, registration_tokens,
 *         pending_requests.
 * All timestamps are stored as Unix epoch seconds (INTEGER).
 * Migration: Adds tenant_id FK to schools, teachers, locations, computers.
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

CREATE TABLE IF NOT EXISTS tenants (
    id           TEXT PRIMARY KEY,
    slug         TEXT UNIQUE NOT NULL,
    name         TEXT NOT NULL,
    owner_email  TEXT UNIQUE NOT NULL,
    status       TEXT DEFAULT 'pending',
    plan         TEXT DEFAULT 'trial',
    created_at   INTEGER NOT NULL,
    activated_at INTEGER
);

CREATE TABLE IF NOT EXISTS registration_tokens (
    token      TEXT PRIMARY KEY,
    tenant_id  TEXT NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    expires_at INTEGER NOT NULL,
    used       INTEGER DEFAULT 0
);

CREATE TABLE IF NOT EXISTS classes (
    id          TEXT PRIMARY KEY,
    tenant_id   TEXT NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    school_id   TEXT REFERENCES schools(id) ON DELETE CASCADE,  -- nullable: owner can create class without a school
    name        TEXT NOT NULL,
    teacher_id  TEXT REFERENCES teachers(id) ON DELETE SET NULL,
    created_at  INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS students (
    id               TEXT PRIMARY KEY,
    tenant_id        TEXT NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    class_id         TEXT NOT NULL REFERENCES classes(id) ON DELETE CASCADE,
    full_name        TEXT NOT NULL,
    username         TEXT NOT NULL,
    password_hash    TEXT NOT NULL,
    machine_id       TEXT,
    is_activated     INTEGER DEFAULT 0,
    activated_at     INTEGER,
    created_at       INTEGER NOT NULL,
    UNIQUE(tenant_id, username)
);

CREATE INDEX IF NOT EXISTS idx_students_tenant  ON students(tenant_id);
CREATE INDEX IF NOT EXISTS idx_students_class   ON students(class_id);
CREATE INDEX IF NOT EXISTS idx_classes_tenant   ON classes(tenant_id);

-- Pending requests: subordinates (teachers, students) submit change_password or other
-- requests; superiors (owner, teacher) accept or reject via inbox UI.
CREATE TABLE IF NOT EXISTS pending_requests (
    id            TEXT PRIMARY KEY,
    tenant_id     TEXT NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    from_id       TEXT NOT NULL,   -- subject UUID (teacher_id or student_id)
    from_role     TEXT NOT NULL,   -- 'teacher' | 'student'
    to_id         TEXT NOT NULL,   -- recipient UUID (teacher_id or owner_id/tenant_id)
    to_role       TEXT NOT NULL,   -- 'teacher' | 'owner'
    type          TEXT NOT NULL,   -- 'change_password'
    payload       TEXT NOT NULL,   -- JSON: {"password_hash": "argon2id:..."}
    status        TEXT DEFAULT 'pending', -- 'pending' | 'accepted' | 'rejected'
    created_at    INTEGER NOT NULL,
    resolved_at   INTEGER
);

CREATE INDEX IF NOT EXISTS idx_pending_requests_to_id     ON pending_requests(to_id);
CREATE INDEX IF NOT EXISTS idx_pending_requests_from_id   ON pending_requests(from_id);
CREATE INDEX IF NOT EXISTS idx_pending_requests_tenant    ON pending_requests(tenant_id);
)";

} // anonymous namespace

// ---------------------------------------------------------------------------
// Pimpl struct
// ---------------------------------------------------------------------------

struct DatabaseManager::Impl
{
    sqlite3*    db = nullptr;
    std::string dataDir;
    std::mutex  dbMutex;
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

    // Performance and durability settings — check return codes
    auto execPragma = [&](const char* sql, const char* name) {
        int prc = sqlite3_exec(m_impl->db, sql, nullptr, nullptr, nullptr);
        if (prc != SQLITE_OK) {
            spdlog::warn("[DatabaseManager] {} failed: {} (non-fatal)",
                         name, sqlite3_errmsg(m_impl->db));
        }
    };
    execPragma("PRAGMA journal_mode=WAL;",   "journal_mode=WAL");
    execPragma("PRAGMA synchronous=NORMAL;", "synchronous=NORMAL");
    execPragma("PRAGMA foreign_keys=ON;",    "foreign_keys=ON");
    execPragma("PRAGMA busy_timeout=5000;",  "busy_timeout=5000");

    createSchema();
    runMigrations();

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
 * IMPORTANT: Callers MUST hold dbMutex() during any sqlite3 operations.
 */
sqlite3* DatabaseManager::schoolDb() noexcept
{
    return m_impl->db;
}

/**
 * @brief Returns the mutex that protects all sqlite3 operations.
 *
 * All repository methods must lock this before sqlite3_prepare/step/finalize.
 */
std::mutex& DatabaseManager::dbMutex() noexcept
{
    return m_impl->dbMutex;
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
        spdlog::error("[DatabaseManager] failed to create schema: {} — database disabled",
                      errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        sqlite3_close(m_impl->db);
        m_impl->db = nullptr;
    } else {
        spdlog::debug("[DatabaseManager] schema created/verified successfully");
    }
}

// ---------------------------------------------------------------------------
// Private: idempotent ALTER TABLE migrations
// ---------------------------------------------------------------------------

/**
 * @brief Adds tenant_id columns to pre-existing tables if absent.
 *
 * SQLite versions before 3.35 do not support ADD COLUMN IF NOT EXISTS,
 * so we use PRAGMA table_info() to check first, then ALTER TABLE only
 * when the column is missing. This makes the migration fully idempotent.
 */
void DatabaseManager::runMigrations()
{
    if (!m_impl->db) return;

    // Returns true when @p column already exists in @p table.
    auto columnExists = [&](const char* table, const char* column) -> bool {
        std::string sql = std::string("PRAGMA table_info(") + table + ")";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_impl->db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        bool found = false;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* colName =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (colName && std::string_view{colName} == column) {
                found = true;
                break;
            }
        }
        sqlite3_finalize(stmt);
        return found;
    };

    // Runs an ALTER TABLE statement; logs a warning on failure (non-fatal).
    auto addColumn = [&](const char* alterSql) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(m_impl->db, alterSql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            spdlog::warn("[DatabaseManager] migration failed (non-fatal): {}",
                         errMsg ? errMsg : "?");
            sqlite3_free(errMsg);
        }
    };

    if (!columnExists("schools", "tenant_id")) {
        addColumn("ALTER TABLE schools ADD COLUMN tenant_id TEXT REFERENCES tenants(id) ON DELETE CASCADE");
    }
    if (!columnExists("teachers", "tenant_id")) {
        addColumn("ALTER TABLE teachers ADD COLUMN tenant_id TEXT NOT NULL DEFAULT ''");
    }
    if (!columnExists("locations", "tenant_id")) {
        addColumn("ALTER TABLE locations ADD COLUMN tenant_id TEXT REFERENCES tenants(id) ON DELETE CASCADE");
    }
    if (!columnExists("computers", "tenant_id")) {
        addColumn("ALTER TABLE computers ADD COLUMN tenant_id TEXT REFERENCES tenants(id) ON DELETE CASCADE");
    }

    // Create indexes for legacy tables' newly added tenant_id columns.
    // These must run AFTER the ALTER TABLE statements to avoid "column does not exist" errors.
    auto execSql = [&](const char* sql) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(m_impl->db, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            spdlog::warn("[DatabaseManager] migration SQL failed (non-fatal): {}",
                         errMsg ? errMsg : "?");
            sqlite3_free(errMsg);
        }
    };

    execSql("CREATE INDEX IF NOT EXISTS idx_schools_tenant ON schools(tenant_id)");
    execSql("CREATE INDEX IF NOT EXISTS idx_teachers_tenant ON teachers(tenant_id)");
    execSql("CREATE INDEX IF NOT EXISTS idx_locations_tenant ON locations(tenant_id)");
    execSql("CREATE INDEX IF NOT EXISTS idx_computers_tenant ON computers(tenant_id)");

    spdlog::debug("[DatabaseManager] migrations applied/verified");
}

} // namespace hub32api::db
