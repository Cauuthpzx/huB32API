#include "../core/PrecompiledHeader.hpp"
#include "HeartbeatMonitor.hpp"
#include "AgentRegistry.hpp"

namespace hub32api::agent {

HeartbeatMonitor::HeartbeatMonitor(AgentRegistry& registry,
                                   std::chrono::milliseconds checkInterval,
                                   OfflineCallback onOffline)
    : m_registry(registry)
    , m_checkInterval(checkInterval)
    , m_onOffline(std::move(onOffline))
{
}

HeartbeatMonitor::~HeartbeatMonitor()
{
    stop();
}

void HeartbeatMonitor::start(std::chrono::milliseconds timeout)
{
    if (m_running.load()) {
        spdlog::warn("[HeartbeatMonitor] already running — ignoring duplicate start()");
        return;
    }
    m_timeout  = timeout;
    m_running  = true;
    m_thread   = std::thread(&HeartbeatMonitor::monitorLoop, this);
}

void HeartbeatMonitor::stop()
{
    if (!m_running.exchange(false)) {
        return; // already stopped
    }

    {
        std::lock_guard<std::mutex> lock(m_cvMutex);
        m_cv.notify_all();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void HeartbeatMonitor::monitorLoop()
{
    while (m_running) {
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            m_cv.wait_for(lock, m_checkInterval, [this] { return !m_running.load(); });
        }

        if (!m_running) {
            break;
        }

        auto agents = m_registry.listAgents();
        auto now    = std::chrono::system_clock::now();

        for (const auto& agent : agents) {
            if (agent.state == AgentState::Offline) {
                continue;
            }

            auto elapsed = now - agent.lastHeartbeat;
            if (elapsed > m_timeout) {
                m_registry.updateState(agent.agentId, AgentState::Offline);
                m_onOffline(agent.agentId);

                auto elapsed_seconds =
                    std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
                spdlog::warn("[HeartbeatMonitor] agent {} offline (no heartbeat for {}s)",
                             agent.agentId, elapsed_seconds);
            }
        }
    }
}

} // namespace hub32api::agent
