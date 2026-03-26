#pragma once

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace hub32api::server {

/**
 * @brief Manages Server-Sent Events (SSE) connections for real-time push.
 *
 * Supports multiple named channels (e.g., "screen", "events", "metrics").
 * Thread-safe: broadcast() can be called from any thread.
 */
class SseManager
{
public:
    using SinkFn = std::function<bool(const std::string& data)>;

    SseManager() = default;

    /**
     * @brief Register a new SSE client on a channel.
     * @param channel  Channel name (e.g., "screen:<computerId>", "events")
     * @param clientId Unique client identifier
     * @param sink     Callback to send data to the client; returns false if disconnected
     */
    void subscribe(const std::string& channel, const std::string& clientId, SinkFn sink);

    /**
     * @brief Remove a client from a channel.
     */
    void unsubscribe(const std::string& channel, const std::string& clientId);

    /**
     * @brief Broadcast an SSE event to all subscribers on a channel.
     * @param channel   Target channel
     * @param event     SSE event type (e.g., "framebuffer", "feature-changed")
     * @param data      Event data (JSON string)
     * @return Number of clients that received the event
     */
    int broadcast(const std::string& channel, const std::string& event, const std::string& data);

    /**
     * @brief Returns the number of active subscribers on a channel.
     */
    int subscriberCount(const std::string& channel) const;

private:
    struct Client
    {
        std::string id;
        SinkFn      sink;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::vector<Client>> m_channels;
};

} // namespace hub32api::server
