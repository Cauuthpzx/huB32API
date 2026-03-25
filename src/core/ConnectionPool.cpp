#include "PrecompiledHeader.hpp"
#include "internal/ConnectionPool.hpp"

namespace hub32api::core::internal {

struct ConnectionPool::Entry
{
    std::string hostname;
    Port        port = 0;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point lastUsedAt;
    // TODO: std::unique_ptr<Hub32Connection> connection;
};

ConnectionPool::ConnectionPool(Limits limits)
    : m_limits(limits)
{}

ConnectionPool::~ConnectionPool()
{
    std::lock_guard lock(m_mutex);
    m_entries.clear();
}

Result<Uid> ConnectionPool::acquire(const std::string& hostname, Port port)
{
    std::lock_guard lock(m_mutex);

    // TODO: Check global limit
    // TODO: Check per-host limit
    // TODO: Find idle existing connection or create new Hub32Connection
    // TODO: Return connection token (UUID)

    spdlog::debug("[ConnectionPool] acquire stub for {}:{}", hostname, port);
    return Result<Uid>::fail(ApiError{
        ErrorCode::NotImplemented,
        "ConnectionPool::acquire not yet implemented"
    });
}

void ConnectionPool::release(const Uid& token)
{
    std::lock_guard lock(m_mutex);
    spdlog::debug("[ConnectionPool] release stub token={}", token);
    // TODO: Mark connection as idle, update lastUsedAt
}

void ConnectionPool::evictExpired()
{
    std::lock_guard lock(m_mutex);
    // TODO: Iterate entries, remove expired/idle
}

int ConnectionPool::activeCount() const noexcept
{
    std::lock_guard lock(m_mutex);
    return static_cast<int>(m_entries.size());
}

int ConnectionPool::hostCount() const noexcept
{
    // TODO: count unique hostnames
    return 0;
}

} // namespace hub32api::core::internal
