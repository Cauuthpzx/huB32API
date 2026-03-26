#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <vector>
#include <nlohmann/json.hpp>
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::media {

class SfuBackend;

struct RoomInfo {
    std::string locationId;
    std::string routerId;
    int producerCount = 0;
    int consumerCount = 0;
};

/// Thread-safe manager that maps location IDs to mediasoup Routers.
/// One Router per room, created on first access and destroyed when explicitly
/// torn down. Producer/consumer counts are tracked for monitoring.
class HUB32API_EXPORT RoomManager
{
public:
    explicit RoomManager(SfuBackend& backend);
    ~RoomManager();

    /// Get or create a Router for a location. Creates on first access.
    Result<std::string> getOrCreateRouter(const std::string& locationId);

    /// Get RTP capabilities for a location's Router.
    Result<nlohmann::json> getRtpCapabilities(const std::string& locationId);

    /// Destroy a room's Router when all participants leave.
    void destroyRoom(const std::string& locationId);

    /// Check if a room has an active Router.
    bool hasRoom(const std::string& locationId) const;

    /// Get count of active rooms.
    size_t roomCount() const;

    /// List all active rooms.
    std::vector<RoomInfo> listRooms() const;

    /// Increment/decrement producer/consumer counts.
    void addProducer(const std::string& locationId);
    void removeProducer(const std::string& locationId);
    void addConsumer(const std::string& locationId);
    void removeConsumer(const std::string& locationId);

private:
    SfuBackend& m_backend;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, RoomInfo> m_rooms; // locationId -> RoomInfo
};

} // namespace hub32api::media
