/**
 * @file ConnectionPool.cpp
 * @brief Implementation of ConnectionPool — manages pooled connection tracking
 *        with per-host and global limits, idle eviction, and lifetime enforcement.
 *
 * Connections are tracked by UUID tokens. No actual VNC connections are
 * established; this layer manages the metadata and enforces limits. Each
 * entry tracks hostname, port, creation time, last-used time, and idle state.
 */

#include "PrecompiledHeader.hpp"
#include "internal/ConnectionPool.hpp"

#include <random>
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace hub32api::core::internal {

namespace {

/**
 * @brief Generates a UUID v4 string using std::mt19937_64.
 *
 * The UUID is formatted as the standard 8-4-4-4-12 hex representation.
 * Uses a thread_local generator for thread safety without a mutex.
 *
 * @return A UUID string, e.g. "550e8400-e29b-41d4-a716-446655440000".
 */
std::string generateUuid()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    const uint64_t hi = dist(rng);
    const uint64_t lo = dist(rng);

    // Set version 4 bits and variant bits per RFC 4122
    const uint64_t hi4 = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    const uint64_t lo4 = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0')
        << std::setw(8)  << ((hi4 >> 32) & 0xFFFFFFFFULL)
        << '-'
        << std::setw(4)  << ((hi4 >> 16) & 0xFFFFULL)
        << '-'
        << std::setw(4)  << (hi4 & 0xFFFFULL)
        << '-'
        << std::setw(4)  << ((lo4 >> 48) & 0xFFFFULL)
        << '-'
        << std::setw(12) << (lo4 & 0x0000FFFFFFFFFFFFULL);
    return oss.str();
}

} // anonymous namespace

/**
 * @brief Internal entry representing a tracked connection in the pool.
 */
struct ConnectionPool::Entry
{
    std::string hostname;                             ///< Target hostname
    Port        port = 0;                             ///< Target port
    std::chrono::steady_clock::time_point createdAt;  ///< When the entry was created
    std::chrono::steady_clock::time_point lastUsedAt; ///< When the entry was last used or released
    bool        idle = false;                         ///< True if the connection has been released back to the pool
    // TODO: std::unique_ptr<Hub32Connection> connection;
};

/**
 * @brief Constructs a ConnectionPool with the given limits.
 *
 * @param limits Per-host and global connection limits, lifetime, and idle timeout.
 */
ConnectionPool::ConnectionPool(Limits limits)
    : m_limits(limits)
{
    spdlog::info("[ConnectionPool] created (perHost={}, global={}, lifetimeSec={}, idleSec={})",
                 m_limits.perHost, m_limits.global, m_limits.lifetimeSec, m_limits.idleSec);
}

/**
 * @brief Destroys the ConnectionPool, clearing all tracked entries.
 */
ConnectionPool::~ConnectionPool()
{
    std::lock_guard lock(m_mutex);
    const auto count = m_entries.size();
    m_entries.clear();
    spdlog::info("[ConnectionPool] destroyed ({} entries cleared)", count);
}

/**
 * @brief Acquires a connection token for the given hostname and port.
 *
 * Checks the global connection limit and the per-host limit. If both are
 * within bounds, creates a new Entry with the current timestamp and returns
 * a UUID token identifying the connection.
 *
 * @param hostname Target hostname to connect to.
 * @param port     Target port number.
 * @return Result containing the connection token UID on success, or an
 *         ApiError with ConnectionLimitReached if limits are exceeded.
 */
Result<Uid> ConnectionPool::acquire(const std::string& hostname, Port port)
{
    std::lock_guard lock(m_mutex);

    // Check global limit
    if (static_cast<int>(m_entries.size()) >= m_limits.global)
    {
        spdlog::warn("[ConnectionPool] global connection limit reached ({}/{})",
                     m_entries.size(), m_limits.global);
        return Result<Uid>::fail(ApiError{
            ErrorCode::ConnectionLimitReached,
            "Global connection limit reached"
        });
    }

    // Count entries for this hostname and check per-host limit
    int hostEntryCount = 0;
    for (const auto& [token, entry] : m_entries)
    {
        if (entry->hostname == hostname)
        {
            ++hostEntryCount;
        }
    }

    if (hostEntryCount >= m_limits.perHost)
    {
        spdlog::warn("[ConnectionPool] per-host connection limit reached for {} ({}/{})",
                     hostname, hostEntryCount, m_limits.perHost);
        return Result<Uid>::fail(ApiError{
            ErrorCode::ConnectionLimitReached,
            "Per-host connection limit reached for " + hostname
        });
    }

    // Generate a UUID token and create the entry
    const auto now = std::chrono::steady_clock::now();
    Uid token = generateUuid();

    auto entry = std::make_unique<Entry>();
    entry->hostname   = hostname;
    entry->port       = port;
    entry->createdAt  = now;
    entry->lastUsedAt = now;
    entry->idle       = false;

    m_entries.emplace(token, std::move(entry));

    spdlog::debug("[ConnectionPool] acquired token={} for {}:{} (total={})",
                  token, hostname, port, m_entries.size());
    return Result<Uid>::ok(token);
}

/**
 * @brief Releases a connection back to the pool, marking it as idle.
 *
 * Updates the lastUsedAt timestamp and sets the idle flag. If the token
 * is not found, logs a warning.
 *
 * @param token The connection token UID to release.
 */
void ConnectionPool::release(const Uid& token)
{
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(token);
    if (it == m_entries.end())
    {
        spdlog::warn("[ConnectionPool] release called for unknown token={}", token);
        return;
    }

    it->second->idle = true;
    it->second->lastUsedAt = std::chrono::steady_clock::now();
    spdlog::debug("[ConnectionPool] released token={} ({}:{})",
                  token, it->second->hostname, it->second->port);
}

/**
 * @brief Evicts expired and idle connections from the pool.
 *
 * Removes entries that have exceeded the connection lifetime (regardless
 * of idle state) or that are idle and have exceeded the idle timeout.
 * Intended to be called periodically by a maintenance timer.
 */
void ConnectionPool::evictExpired()
{
    std::lock_guard lock(m_mutex);

    const auto now = std::chrono::steady_clock::now();
    const auto lifetimeLimit = std::chrono::seconds(m_limits.lifetimeSec);
    const auto idleLimit     = std::chrono::seconds(m_limits.idleSec);

    int evicted = 0;
    for (auto it = m_entries.begin(); it != m_entries.end(); )
    {
        const auto& entry = it->second;
        const bool lifetimeExpired = (now - entry->createdAt) > lifetimeLimit;
        const bool idleExpired     = entry->idle && (now - entry->lastUsedAt) > idleLimit;

        if (lifetimeExpired || idleExpired)
        {
            spdlog::debug("[ConnectionPool] evicting token={} ({}:{}, lifetime={}, idle={})",
                          it->first, entry->hostname, entry->port,
                          lifetimeExpired, idleExpired);
            it = m_entries.erase(it);
            ++evicted;
        }
        else
        {
            ++it;
        }
    }

    if (evicted > 0)
    {
        spdlog::info("[ConnectionPool] evicted {} expired entries ({} remaining)",
                     evicted, m_entries.size());
    }
}

/**
 * @brief Returns the number of active (non-idle) connections.
 *
 * @return Count of entries where idle == false.
 */
int ConnectionPool::activeCount() const noexcept
{
    std::lock_guard lock(m_mutex);
    int count = 0;
    for (const auto& [token, entry] : m_entries)
    {
        if (!entry->idle)
        {
            ++count;
        }
    }
    return count;
}

/**
 * @brief Returns the number of unique hostnames with active connections.
 *
 * @return Count of distinct hostnames across all entries in the pool.
 */
int ConnectionPool::hostCount() const noexcept
{
    std::lock_guard lock(m_mutex);
    std::unordered_set<std::string> hosts;
    for (const auto& [token, entry] : m_entries)
    {
        hosts.insert(entry->hostname);
    }
    return static_cast<int>(hosts.size());
}

} // namespace hub32api::core::internal
