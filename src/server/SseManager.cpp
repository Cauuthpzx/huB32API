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
    // Format SSE message — each data line must be prefixed with "data: "
    std::string msg;
    msg.reserve(event.size() + data.size() + 32);
    msg += "event: " + event + "\n";

    // Handle multiline data correctly per SSE spec
    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
        msg += "data: " + line + "\n";
    }
    if (data.empty()) {
        msg += "data: \n";
    }
    msg += "\n";

    // Copy clients under lock, send outside lock to avoid blocking
    std::vector<Client> snapshot;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_channels.find(channel);
        if (it == m_channels.end()) return 0;
        snapshot = it->second;
    }

    // Send to all clients outside lock — avoids blocking on slow I/O
    int sent = 0;
    std::vector<std::string> failedIds;
    for (auto& c : snapshot) {
        if (c.sink(msg)) {
            ++sent;
        } else {
            spdlog::debug("[SseManager] client '{}' disconnected from '{}'", c.id, channel);
            failedIds.push_back(c.id);
        }
    }

    // Remove failed clients under lock
    if (!failedIds.empty()) {
        std::lock_guard lock(m_mutex);
        auto it = m_channels.find(channel);
        if (it != m_channels.end()) {
            auto& clients = it->second;
            clients.erase(
                std::remove_if(clients.begin(), clients.end(),
                    [&](const Client& c) {
                        return std::find(failedIds.begin(), failedIds.end(), c.id) != failedIds.end();
                    }),
                clients.end());
            if (clients.empty()) m_channels.erase(it);
        }
    }

    return sent;
}

int SseManager::subscriberCount(const std::string& channel) const
{
    std::lock_guard lock(m_mutex);
    auto it = m_channels.find(channel);
    return (it != m_channels.end()) ? static_cast<int>(it->second.size()) : 0;
}

} // namespace hub32api::server
