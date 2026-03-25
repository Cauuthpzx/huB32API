#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace hub32api::auth::internal {

/**
 * @brief TokenStore — tracks revoked JWT tokens (denylist).
 *
 * Maintains an in-memory set of revoked JTIs with optional file-based
 * persistence. When a persist path is provided, revoked tokens survive
 * process restarts: the file is loaded at construction, each revocation
 * is appended, and purgeExpired() rewrites the file without stale entries.
 *
 * Each revoked token is stored alongside an expiry timestamp so that
 * purgeExpired() can remove stale entries instead of clearing everything.
 */
class TokenStore
{
public:
    /**
     * @brief Constructs the token store, optionally loading persisted state.
     *
     * @param persistPath  Path to a file used for persistence.
     *                     If empty, the store is in-memory only (previous behaviour).
     */
    explicit TokenStore(const std::string& persistPath = {});

    /** @brief Revokes a token by its JTI, preventing further use. */
    void revoke(const std::string& jti);

    /** @brief Checks whether a token JTI has been revoked. */
    bool isRevoked(const std::string& jti) const noexcept;

    /** @brief Removes revoked entries whose expiry time has passed. */
    void purgeExpired();

private:
    mutable std::mutex                    m_mutex;
    std::unordered_set<std::string>       m_revoked;

    /** @brief Maps each revoked JTI to the time at which it may be purged. */
    std::unordered_map<std::string,
                       std::chrono::steady_clock::time_point> m_expiryTimes;

    /** @brief File path for persistence (empty = in-memory only). */
    std::string m_persistPath;

    /** @brief Loads revoked tokens from the persist file at startup. */
    void loadFromFile();

    /** @brief Rewrites the persist file with all current entries (used after purge). */
    void saveToFile();

    /** @brief Appends a single revoked entry to the persist file. */
    void appendToFile(const std::string& jti, std::chrono::steady_clock::time_point expiry);
};

} // namespace hub32api::auth::internal
