/**
 * @file TokenStore.cpp
 * @brief Implementation of TokenStore — in-memory revoked-token denylist
 *        with time-based expiry tracking.
 *
 * Revoked JTIs are stored alongside a steady_clock expiry timestamp.
 * purgeExpired() removes entries that have passed their expiry, keeping
 * the denylist bounded without losing active revocations.
 */

#include "../../core/PrecompiledHeader.hpp"
#include "TokenStore.hpp"

namespace hub32api::auth::internal {

namespace {

/// Default duration to keep a revoked token entry before it can be purged.
/// Set to 1 hour, which should exceed the longest JWT lifetime in use.
constexpr auto k_defaultRevocationTtl = std::chrono::hours(1);

} // anonymous namespace

/**
 * @brief Revokes a token by its JTI, adding it to the denylist.
 *
 * The revocation entry is stored with an expiry timestamp of now + 1 hour.
 * After that time, purgeExpired() may remove the entry (the JWT itself
 * will have expired by then, so continued tracking is unnecessary).
 *
 * @param jti The unique JWT ID to revoke.
 */
void TokenStore::revoke(const std::string& jti)
{
    std::lock_guard lock(m_mutex);
    m_revoked.insert(jti);
    m_expiryTimes[jti] = std::chrono::steady_clock::now() + k_defaultRevocationTtl;
    spdlog::debug("[TokenStore] revoked jti={}", jti);
}

/**
 * @brief Checks whether a token JTI has been revoked.
 *
 * @param jti The unique JWT ID to check.
 * @return true if the JTI is in the revocation set, false otherwise.
 */
bool TokenStore::isRevoked(const std::string& jti) const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_revoked.count(jti) > 0;
}

/**
 * @brief Removes revoked entries whose expiry time has passed.
 *
 * Iterates the expiry map and removes any entry whose tracked expiry
 * timestamp is in the past. The corresponding entry is also removed
 * from the revoked set. This keeps the denylist bounded over time.
 */
void TokenStore::purgeExpired()
{
    std::lock_guard lock(m_mutex);
    const auto now = std::chrono::steady_clock::now();
    int purged = 0;

    for (auto it = m_expiryTimes.begin(); it != m_expiryTimes.end(); )
    {
        if (now >= it->second)
        {
            m_revoked.erase(it->first);
            it = m_expiryTimes.erase(it);
            ++purged;
        }
        else
        {
            ++it;
        }
    }

    if (purged > 0)
    {
        spdlog::debug("[TokenStore] purged {} expired revocation entries ({} remaining)",
                      purged, m_revoked.size());
    }
}

} // namespace hub32api::auth::internal
