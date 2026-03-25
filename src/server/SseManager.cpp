/**
 * @file SseManager.cpp
 * @brief Server-Sent Events manager for real-time push notifications.
 */

#include "../core/PrecompiledHeader.hpp"
#include "SseManager.hpp"

namespace hub32api::server {

void SseManager::subscribe(const std::string& channel, const std::string& clientId, SinkFn sink)
{
    std::lock_guard lock(m_mutex);
    m_channels[channel].push_back(Client{clientId, std::move(sink)});
    spdlog::debug("[SseManager] client '{}' subscribed to '{}'", clientId, channel);
}

void SseManager::unsubscribe(const std::string& channel, const std::string& clientId)
{
    std::lock_guard lock(m_mutex);
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) return;

    auto& clients = it->second;
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
            [&](const Client& c) { return c.id == clientId; }),
        clients.end());

    if (clients.empty()) m_channels.erase(it);
    spdlog::debug("[SseManager] client '{}' unsubscribed from '{}'", clientId, channel);
}

int SseManager::broadcast(const std::string& channel, const std::string& event, const std::string& data)
{
    // Format SSE message
    std::string msg;
    msg.reserve(event.size() + data.size() + 32);
    msg += "event: " + event + "\n";
    msg += "data: " + data + "\n\n";

    std::lock_guard lock(m_mutex);
    auto it = m_channels.find(channel);
    if (it == m_channels.end()) return 0;

    auto& clients = it->second;
    int sent = 0;

    // Send to all clients, remove disconnected ones
    clients.erase(
        std::remove_if(clients.begin(), clients.end(),
            [&](Client& c) {
                if (c.sink(msg)) {
                    ++sent;
                    return false;  // keep
                }
                spdlog::debug("[SseManager] client '{}' disconnected from '{}'", c.id, channel);
                return true;  // remove
            }),
        clients.end());

    if (clients.empty()) m_channels.erase(it);
    return sent;
}

int SseManager::subscriberCount(const std::string& channel) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_channels.find(channel);
    return (it != m_channels.end()) ? static_cast<int>(it->second.size()) : 0;
}

} // namespace hub32api::server
