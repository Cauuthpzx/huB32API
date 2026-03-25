/**
 * @file TokenStore.cpp
 * @brief Implementation of TokenStore — revoked-token denylist with optional
 *        file-based persistence and time-based expiry tracking.
 *
 * Revoked JTIs are stored alongside a steady_clock expiry timestamp.
 * purgeExpired() removes entries that have passed their expiry, keeping
 * the denylist bounded without losing active revocations.
 *
 * When a persist path is configured the store additionally:
 *  - Loads previously revoked tokens from the file at construction.
 *  - Appends new revocations to the file (one line per entry).
 *  - Rewrites the file after purging to remove stale entries.
 *
 * File format: one line per entry — <jti>\t<expiry_epoch_seconds>\n
 */

#include "../../core/PrecompiledHeader.hpp"
#include "TokenStore.hpp"

#include <fstream>

namespace hub32api::auth::internal {

namespace {

/// Default duration to keep a revoked token entry before it can be purged.
/// Set to 1 hour, which should exceed the longest JWT lifetime in use.
constexpr auto k_defaultRevocationTtl = std::chrono::hours(1);

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

/**
 * @brief Constructs the TokenStore, optionally loading persisted state.
 *
 * If @p persistPath is non-empty, any previously revoked tokens stored in
 * that file are loaded into the in-memory denylist. Expired entries in the
 * file are silently skipped.
 *
 * @param persistPath  Path to the persistence file, or empty for in-memory only.
 */
TokenStore::TokenStore(const std::string& persistPath)
    : m_persistPath(persistPath)
{
    if (!m_persistPath.empty()) {
        loadFromFile();
    }
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

/**
 * @brief Revokes a token by its JTI, adding it to the denylist.
 *
 * The revocation entry is stored with an expiry timestamp of now + 1 hour.
 * After that time, purgeExpired() may remove the entry (the JWT itself
 * will have expired by then, so continued tracking is unnecessary).
 *
 * If a persist path is configured the entry is also appended to the file.
 *
 * @param jti The unique JWT ID to revoke.
 */
void TokenStore::revoke(const std::string& jti)
{
    std::lock_guard lock(m_mutex);
    m_revoked.insert(jti);
    auto expiry = std::chrono::steady_clock::now() + k_defaultRevocationTtl;
    m_expiryTimes[jti] = expiry;
    appendToFile(jti, expiry);
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
 *
 * If entries were purged and a persist path is configured, the file is
 * rewritten to exclude the expired entries.
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
        saveToFile();  // Rewrite file without expired entries
        spdlog::debug("[TokenStore] purged {} expired revocation entries ({} remaining)",
                      purged, m_revoked.size());
    }
}

// ---------------------------------------------------------------------------
// File persistence (private)
// ---------------------------------------------------------------------------

/**
 * @brief Loads revoked tokens from the persist file.
 *
 * Each line is expected to contain <jti>\t<expiry_epoch_seconds>.
 * Entries whose expiry has already passed are silently skipped.
 * The steady_clock expiry is reconstructed as an approximate offset
 * from the current time.
 */
void TokenStore::loadFromFile()
{
    std::ifstream f(m_persistPath);
    if (!f.is_open()) return;

    std::string line;
    const auto now = std::chrono::steady_clock::now();
    int loaded = 0, skipped = 0;

    while (std::getline(f, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;

        std::string jti = line.substr(0, tab);
        int64_t epochSec = 0;
        try { epochSec = std::stoll(line.substr(tab + 1)); } catch (...) { continue; }

        // Convert epoch seconds to steady_clock point (approximate)
        auto expiry = now + std::chrono::seconds(epochSec -
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count());

        if (expiry > now) {
            m_revoked.insert(jti);
            m_expiryTimes[jti] = expiry;
            ++loaded;
        } else {
            ++skipped;
        }
    }
    spdlog::info("[TokenStore] loaded {} revoked tokens from '{}' ({} expired, skipped)",
                 loaded, m_persistPath, skipped);
}

/**
 * @brief Rewrites the persist file with all current (non-expired) entries.
 *
 * Called after purgeExpired() removes stale entries, so the file stays
 * bounded and does not grow without limit.
 */
void TokenStore::saveToFile()
{
    if (m_persistPath.empty()) return;
    std::ofstream f(m_persistPath, std::ios::trunc);
    if (!f.is_open()) return;

    for (const auto& [jti, expiry] : m_expiryTimes) {
        auto epochSec = std::chrono::duration_cast<std::chrono::seconds>(
            expiry.time_since_epoch()).count();
        f << jti << '\t' << epochSec << '\n';
    }
}

/**
 * @brief Appends a single revoked entry to the persist file.
 *
 * Uses std::ios::app so that concurrent revocations do not overwrite
 * each other (the mutex is already held by the caller).
 */
void TokenStore::appendToFile(const std::string& jti,
                               std::chrono::steady_clock::time_point expiry)
{
    if (m_persistPath.empty()) return;
    std::ofstream f(m_persistPath, std::ios::app);
    if (!f.is_open()) return;
    auto epochSec = std::chrono::duration_cast<std::chrono::seconds>(
        expiry.time_since_epoch()).count();
    f << jti << '\t' << epochSec << '\n';
}

} // namespace hub32api::auth::internal
