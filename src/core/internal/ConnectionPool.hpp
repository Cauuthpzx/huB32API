#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "hub32api/export.h"
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Result.hpp"

namespace hub32api::core::internal {

// -----------------------------------------------------------------------
// ConnectionPool — manages pooled VNC connections to Hub32 servers.
// Thread-safe; enforces per-host and global connection limits.
// Mirrors the connection management in Hub32's WebApiConnection.
// -----------------------------------------------------------------------
class HUB32API_EXPORT ConnectionPool
{
public:
    struct Limits
    {
        int perHost      = 4;
        int global       = 64;
        int lifetimeSec  = 180;
        int idleSec      = 60;
    };

    explicit ConnectionPool(Limits limits);
    ~ConnectionPool();

    // Acquire or create a connection handle (thread-safe)
    // Returns connection token or error
    Result<Uid> acquire(const std::string& hostname, Port port);

    // Release connection back to pool
    void release(const Uid& token);

    // Evict expired / idle connections (called periodically)
    void evictExpired();

    int activeCount() const noexcept;
    int hostCount()   const noexcept;

private:
    struct Entry;
    mutable std::mutex m_mutex;
    std::unordered_map<Uid, std::unique_ptr<Entry>> m_entries;
    Limits m_limits;
};

} // namespace hub32api::core::internal
