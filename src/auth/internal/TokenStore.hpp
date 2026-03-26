#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

struct sqlite3;

namespace hub32api::auth::internal {

/**
 * @brief TokenStore — tracks revoked JWT tokens (denylist).
 *
 * When a dbPath is provided, revoked tokens are persisted in an SQLite
 * database using WAL mode. Each entry carries an expires_at timestamp
 * so that purgeExpired() can remove stale rows and keep the database bounded.
 *
 * When no dbPath is given the store operates in in-memory mode using a simple
 * std::unordered_set (useful for testing and configurations where persistence
 * is not required).
 */
class HUB32API_EXPORT TokenStore
{
public:
    /// @brief Creates a TokenStore. Fails if dbPath is non-empty but DB cannot be opened.
    /// Empty dbPath = in-memory mode (test/dev only).
    static Result<std::unique_ptr<TokenStore>> create(const std::string& dbPath = {});

    ~TokenStore();

    TokenStore(const TokenStore&) = delete;
    TokenStore& operator=(const TokenStore&) = delete;

    /** @brief Revokes a token by its JTI with a default TTL of 1 hour. */
    void revoke(const std::string& jti);

    /**
     * @brief Revokes a token by its JTI with an explicit expiry timestamp.
     *
     * @param jti       The JWT ID to revoke.
     * @param expiresAt The time at which this revocation entry may be purged.
     */
    void revokeWithExpiry(const std::string& jti,
                          std::chrono::system_clock::time_point expiresAt);

    /** @brief Checks whether a token JTI is in the revocation denylist. */
    bool isRevoked(const std::string& jti) const;

    /** @brief Removes revocation entries whose expiry time has passed. */
    void purgeExpired();

private:
    TokenStore() = default;

    mutable std::mutex m_mutex;
    mutable sqlite3*   m_db       = nullptr;
    bool               m_useSqlite = false;

    /** @brief Fallback in-memory set used when no database path is configured. */
    std::unordered_set<std::string> m_memRevoked;

    /** @brief Opens the SQLite database and creates the schema if needed. */
    void initDb(const std::string& dbPath);
};

} // namespace hub32api::auth::internal
