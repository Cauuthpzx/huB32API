#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include "hub32api/core/Types.hpp"
#include "hub32api/core/Constants.hpp"
#include "hub32api/export.h"

namespace hub32api::agent {

class AgentRegistry;

class HUB32API_EXPORT HeartbeatMonitor
{
public:
    using OfflineCallback = std::function<void(const Uid& agentId)>;

    HeartbeatMonitor(AgentRegistry& registry,
                     std::chrono::milliseconds checkInterval,
                     OfflineCallback onOffline);
    ~HeartbeatMonitor();

    HeartbeatMonitor(const HeartbeatMonitor&) = delete;
    HeartbeatMonitor& operator=(const HeartbeatMonitor&) = delete;

    void start(std::chrono::milliseconds timeout);
    void stop();

private:
    void monitorLoop();

    AgentRegistry&             m_registry;
    std::chrono::milliseconds  m_checkInterval;
    std::chrono::milliseconds  m_timeout{std::chrono::milliseconds(hub32api::kDefaultHeartbeatTimeoutMs)};  // milliseconds
    OfflineCallback            m_onOffline;
    std::thread                m_thread;
    std::atomic<bool>          m_running{false};
    std::mutex                 m_cvMutex;
    std::condition_variable    m_cv;
};

} // namespace hub32api::agent
