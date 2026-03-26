#include <gtest/gtest.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "agent/HeartbeatMonitor.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"

using namespace hub32api;
using namespace hub32api::agent;

TEST(HeartbeatMonitor, DetectsOfflineAgent)
{
    AgentRegistry registry;

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<Uid> offlineAgents;

    HeartbeatMonitor monitor(registry, std::chrono::milliseconds(100),
        [&](const Uid& agentId) {
            std::lock_guard<std::mutex> lock(mtx);
            offlineAgents.push_back(agentId);
            cv.notify_all();
        });

    // Register agent — registerAgent sets lastHeartbeat = now.
    // With a 200ms timeout the agent becomes stale quickly.
    AgentInfo info;
    info.agentId  = "agent-001";
    info.hostname = "PC-01";
    registry.registerAgent(info);

    // Start with a very short timeout so the freshly-registered agent
    // goes stale fast (200ms).
    monitor.start(std::chrono::milliseconds(200));

    // Wait up to 2 seconds for the offline callback
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(2),
                    [&] { return !offlineAgents.empty(); });
    }

    monitor.stop();

    EXPECT_FALSE(offlineAgents.empty());
    if (!offlineAgents.empty()) {
        EXPECT_EQ(offlineAgents.front(), "agent-001");
    }
}

TEST(HeartbeatMonitor, DoesNotTriggerForRecentHeartbeat)
{
    AgentRegistry registry;

    std::atomic<int> offlineCount{0};

    HeartbeatMonitor monitor(registry, std::chrono::milliseconds(50),
        [&](const Uid&) { ++offlineCount; });

    AgentInfo info;
    info.agentId  = "agent-002";
    info.hostname = "PC-02";
    registry.registerAgent(info);

    // 60-second timeout — the freshly-registered agent will never expire
    // during the 200ms observation window.
    monitor.start(std::chrono::seconds(60));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    monitor.stop();

    EXPECT_EQ(offlineCount.load(), 0);
}

TEST(HeartbeatMonitor, StopIsClean)
{
    AgentRegistry registry;
    HeartbeatMonitor monitor(registry, std::chrono::milliseconds(50),
        [](const Uid&) {});
    monitor.start(std::chrono::seconds(90));
    monitor.stop(); // should return quickly without hang or crash
}
