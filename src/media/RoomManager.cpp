#include "../core/PrecompiledHeader.hpp"
#include "RoomManager.hpp"
#include "SfuBackend.hpp"

namespace hub32api::media {

RoomManager::RoomManager(SfuBackend& backend)
    : m_backend(backend)
{
    spdlog::info("[RoomManager] initialized with backend: {}", backend.backendName());
}

RoomManager::~RoomManager()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [locationId, room] : m_rooms) {
        spdlog::debug("[RoomManager] destroying room on shutdown: locationId={} routerId={}",
                      locationId, room.routerId);
        m_backend.closeRouter(room.routerId);
    }
    m_rooms.clear();
}

Result<std::string> RoomManager::getOrCreateRouter(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_rooms.find(locationId);
    if (it != m_rooms.end()) {
        spdlog::debug("[RoomManager] getOrCreateRouter: reusing routerId={} for locationId={}",
                      it->second.routerId, locationId);
        return Result<std::string>::ok(it->second.routerId);
    }

    auto result = m_backend.createRouter(locationId);
    if (result.is_err()) {
        spdlog::error("[RoomManager] getOrCreateRouter: createRouter failed for locationId={}: {}",
                      locationId, result.error().message);
        return result;
    }

    const std::string routerId = result.value();
    RoomInfo room;
    room.locationId = locationId;
    room.routerId   = routerId;
    m_rooms[locationId] = std::move(room);

    spdlog::info("[RoomManager] created router for locationId={} routerId={}", locationId, routerId);
    return Result<std::string>::ok(routerId);
}

Result<nlohmann::json> RoomManager::getRtpCapabilities(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_rooms.find(locationId);
    if (it == m_rooms.end()) {
        return Result<nlohmann::json>::fail(
            ApiError{ErrorCode::NotFound, "No router found for location: " + locationId});
    }

    return m_backend.getRouterRtpCapabilities(it->second.routerId);
}

void RoomManager::destroyRoom(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_rooms.find(locationId);
    if (it == m_rooms.end()) {
        spdlog::debug("[RoomManager] destroyRoom: locationId={} not found (already destroyed?)",
                      locationId);
        return;
    }

    spdlog::info("[RoomManager] destroyRoom: locationId={} routerId={}",
                 locationId, it->second.routerId);
    m_backend.closeRouter(it->second.routerId);
    m_rooms.erase(it);
}

bool RoomManager::hasRoom(const std::string& locationId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rooms.find(locationId) != m_rooms.end();
}

size_t RoomManager::roomCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rooms.size();
}

std::vector<RoomInfo> RoomManager::listRooms() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<RoomInfo> result;
    result.reserve(m_rooms.size());
    for (const auto& [locationId, room] : m_rooms) {
        result.push_back(room);
    }
    return result;
}

void RoomManager::addProducer(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rooms.find(locationId);
    if (it != m_rooms.end()) {
        ++it->second.producerCount;
        spdlog::debug("[RoomManager] addProducer: locationId={} producerCount={}",
                      locationId, it->second.producerCount);
    }
}

void RoomManager::removeProducer(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rooms.find(locationId);
    if (it != m_rooms.end() && it->second.producerCount > 0) {
        --it->second.producerCount;
        spdlog::debug("[RoomManager] removeProducer: locationId={} producerCount={}",
                      locationId, it->second.producerCount);
    }
}

void RoomManager::addConsumer(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rooms.find(locationId);
    if (it != m_rooms.end()) {
        ++it->second.consumerCount;
        spdlog::debug("[RoomManager] addConsumer: locationId={} consumerCount={}",
                      locationId, it->second.consumerCount);
    }
}

void RoomManager::removeConsumer(const std::string& locationId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_rooms.find(locationId);
    if (it != m_rooms.end() && it->second.consumerCount > 0) {
        --it->second.consumerCount;
        spdlog::debug("[RoomManager] removeConsumer: locationId={} consumerCount={}",
                      locationId, it->second.consumerCount);
    }
}

} // namespace hub32api::media
