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
 * In-memory store; flushed on restart. Sufficient for single-process deployment.
 * Each revoked token is stored alongside an expiry timestamp so that
 * purgeExpired() can remove stale entries instead of clearing everything.
 */
class TokenStore
{
public:
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
};

} // namespace hub32api::auth::internal
