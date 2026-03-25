#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "veyon32api/export.h"
#include "veyon32api/core/Types.hpp"
#include "veyon32api/core/Result.hpp"

namespace veyon32api::core::internal {

// -----------------------------------------------------------------------
// ConnectionPool — manages pooled VNC connections to Veyon servers.
// Thread-safe; enforces per-host and global connection limits.
// Mirrors the connection management in Veyon's WebApiConnection.
// -----------------------------------------------------------------------
class VEYON32API_EXPORT ConnectionPool
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

} // namespace veyon32api::core::internal
