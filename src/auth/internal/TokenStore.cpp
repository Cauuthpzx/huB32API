/**
 * @file TokenStore.cpp
 * @brief Implementation of TokenStore — revoked-token denylist backed by
 *        SQLite (with WAL mode) or an in-memory set when no path is given.
 *
 * Schema:
 *   revoked_tokens(
 *       jti        TEXT PRIMARY KEY,
 *       revoked_at INTEGER NOT NULL,
 *       expires_at INTEGER NOT NULL
 *   )
 *
 * All timestamps are stored as Unix epoch seconds (INTEGER).
 * All SQLite operations use prepared statements — no string concatenation.
 */

#include "../../core/PrecompiledHeader.hpp"
#include "TokenStore.hpp"

#include <sqlite3.h>

namespace hub32api::auth::internal {

namespace {

/// Default duration a revocation entry is retained before purging.
/// Must exceed the maximum JWT lifetime so the token cannot be re-used
/// after the entry is deleted.
constexpr auto k_defaultRevocationTtl = std::chrono::hours(1);

/**
 * @brief Converts a system_clock time_point to Unix epoch seconds.
 */
inline int64_t toEpochSeconds(std::chrono::system_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        tp.time_since_epoch()).count();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TokenStore::~TokenStore()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

Result<std::unique_ptr<TokenStore>> TokenStore::create(const std::string& dbPath)
{
    auto store = std::unique_ptr<TokenStore>(new TokenStore());

    if (!dbPath.empty()) {
        store->initDb(dbPath);
        if (!store->m_useSqlite) {
            return Result<std::unique_ptr<TokenStore>>::fail(ApiError{
                ErrorCode::InternalError,
                "[TokenStore] Failed to open revocation database: " + dbPath
            });
        }
    } else {
        spdlog::info("[TokenStore] in-memory mode (no dbPath configured)");
    }

    return Result<std::unique_ptr<TokenStore>>::ok(std::move(store));
}

// ---------------------------------------------------------------------------
// Private: database initialisation
// ---------------------------------------------------------------------------

/**
 * @brief Opens the SQLite database, enables WAL mode, and creates the schema.
 *
 * On failure, logs the error and leaves m_useSqlite = false so the in-memory
 * fallback is used automatically.
 *
 * @param dbPath  Path to the SQLite database file.
 */
void TokenStore::initDb(const std::string& dbPath)
{
    int rc = sqlite3_open(dbPath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        spdlog::error("[TokenStore] sqlite3_open('{}') failed: {}",
                      dbPath, sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    // Enable WAL mode for better concurrent read/write performance
    sqlite3_exec(m_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Create the revocation table
    constexpr const char* k_createTable =
        "CREATE TABLE IF NOT EXISTS revoked_tokens ("
        "  jti        TEXT    PRIMARY KEY,"
        "  revoked_at INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL"
        ");";

    constexpr const char* k_createIndex =
        "CREATE INDEX IF NOT EXISTS idx_revoked_tokens_expires_at "
        "ON revoked_tokens(expires_at);";

    char* errMsg = nullptr;
    rc = sqlite3_exec(m_db, k_createTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::error("[TokenStore] failed to create table: {}",
                      errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    rc = sqlite3_exec(m_db, k_createIndex, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        spdlog::warn("[TokenStore] failed to create index: {}",
                     errMsg ? errMsg : "unknown error");
        sqlite3_free(errMsg);
        // Non-fatal: the index is a performance hint only
    }

    m_useSqlite = true;
    spdlog::info("[TokenStore] opened SQLite database '{}'", dbPath);
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Revokes a token JTI with a default TTL of 1 hour.
 *
 * Delegates to revokeWithExpiry() with an expiry of now + 1 hour.
 *
 * @param jti  The JWT ID to add to the denylist.
 */
void TokenStore::revoke(const std::string& jti)
{
    revokeWithExpiry(jti,
        std::chrono::system_clock::now() + k_defaultRevocationTtl);
}

/**
 * @brief Revokes a token JTI with an explicit expiry timestamp.
 *
 * SQLite mode: INSERT OR REPLACE so that re-revocation updates the expiry.
 * In-memory mode: inserts the JTI into the in-memory set (expiry not tracked).
 *
 * @param jti        The JWT ID to revoke.
 * @param expiresAt  The time after which purgeExpired() may remove this entry.
 */
void TokenStore::revokeWithExpiry(const std::string& jti,
                                  std::chrono::system_clock::time_point expiresAt)
{
    std::lock_guard lock(m_mutex);

    if (m_useSqlite) {
        constexpr const char* k_sql =
            "INSERT OR REPLACE INTO revoked_tokens(jti, revoked_at, expires_at) "
            "VALUES(?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            spdlog::error("[TokenStore] revokeWithExpiry prepare failed: {}",
                          sqlite3_errmsg(m_db));
            return;
        }

        const int64_t revokedAt = toEpochSeconds(std::chrono::system_clock::now());
        const int64_t expiresAtSec = toEpochSeconds(expiresAt);

        sqlite3_bind_text(stmt, 1, jti.c_str(), static_cast<int>(jti.size()), SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, revokedAt);
        sqlite3_bind_int64(stmt, 3, expiresAtSec);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            spdlog::error("[TokenStore] revokeWithExpiry step failed: {}",
                          sqlite3_errmsg(m_db));
        }
        sqlite3_finalize(stmt);
    } else {
        m_memRevoked.insert(jti);
    }

    spdlog::debug("[TokenStore] revoked jti={}", jti);
}

/**
 * @brief Checks whether a token JTI is in the revocation denylist.
 *
 * SQLite mode: queries for the JTI only if expires_at is still in the future,
 * so entries that have logically expired (but not yet purged) are not reported
 * as revoked.
 *
 * In-memory mode: checks the in-memory set (no expiry tracking).
 *
 * @param jti  The JWT ID to look up.
 * @return true if the JTI is currently revoked, false otherwise.
 */
bool TokenStore::isRevoked(const std::string& jti) const
{
    std::lock_guard lock(m_mutex);

    if (m_useSqlite) {
        constexpr const char* k_sql =
            "SELECT 1 FROM revoked_tokens "
            "WHERE jti = ? AND expires_at > ? "
            "LIMIT 1;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            spdlog::error("[TokenStore] isRevoked prepare failed: {} "
                          "— treating as revoked (fail-closed)",
                          sqlite3_errmsg(m_db));
            return true;  // FAIL-CLOSED: deny access when DB is unavailable
        }

        const int64_t nowSec = toEpochSeconds(std::chrono::system_clock::now());
        sqlite3_bind_text(stmt, 1, jti.c_str(), static_cast<int>(jti.size()), SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, nowSec);

        const bool found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return found;
    }

    return m_memRevoked.count(jti) > 0;
}

/**
 * @brief Removes revocation entries whose expiry time has passed.
 *
 * SQLite mode: executes a DELETE for all rows where expires_at <= now.
 * In-memory mode: no-op (in-memory set has no expiry tracking).
 */
void TokenStore::purgeExpired()
{
    std::lock_guard lock(m_mutex);

    if (!m_useSqlite) {
        return;
    }

    constexpr const char* k_sql =
        "DELETE FROM revoked_tokens WHERE expires_at <= ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, k_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("[TokenStore] purgeExpired prepare failed: {}",
                      sqlite3_errmsg(m_db));
        return;
    }

    const int64_t nowSec = toEpochSeconds(std::chrono::system_clock::now());
    sqlite3_bind_int64(stmt, 1, nowSec);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        spdlog::error("[TokenStore] purgeExpired step failed: {}",
                      sqlite3_errmsg(m_db));
    } else {
        const int purged = sqlite3_changes(m_db);
        if (purged > 0) {
            spdlog::debug("[TokenStore] purged {} expired revocation entries", purged);
        }
    }

    sqlite3_finalize(stmt);
}

} // namespace hub32api::auth::internal
