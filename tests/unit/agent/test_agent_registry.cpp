#include <gtest/gtest.h>
#include <thread>
#include "agent/AgentRegistry.hpp"

using namespace hub32api;
using namespace hub32api::agent;

TEST(AgentRegistryTest, RegisterAndFind)
{
    AgentRegistry reg;
    AgentInfo info;
    info.agentId   = "agent-001";
    info.hostname  = "PC-Lab-01";
    info.ipAddress = "192.168.1.101";

    auto result = reg.registerAgent(info);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), "agent-001");

    auto found = reg.findAgent("agent-001");
    ASSERT_TRUE(found.is_ok());
    EXPECT_EQ(found.value().hostname, "PC-Lab-01");
    EXPECT_EQ(found.value().state, AgentState::Online);
}

TEST(AgentRegistryTest, RegisterRejectsEmptyId)
{
    AgentRegistry reg;
    AgentInfo info;
    info.agentId = "";

    auto result = reg.registerAgent(info);
    EXPECT_TRUE(result.is_err());
}

TEST(AgentRegistryTest, UnregisterRemovesAgent)
{
    AgentRegistry reg;
    AgentInfo info;
    info.agentId  = "agent-002";
    info.hostname = "PC-Lab-02";

    reg.registerAgent(info);
    reg.unregisterAgent("agent-002");

    auto found = reg.findAgent("agent-002");
    EXPECT_TRUE(found.is_err());
}

TEST(AgentRegistryTest, ListOnlineAgents)
{
    AgentRegistry reg;

    AgentInfo a1; a1.agentId = "a1"; a1.hostname = "h1"; a1.state = AgentState::Online;
    AgentInfo a2; a2.agentId = "a2"; a2.hostname = "h2"; a2.state = AgentState::Offline;
    AgentInfo a3; a3.agentId = "a3"; a3.hostname = "h3"; a3.state = AgentState::Online;

    reg.registerAgent(a1);
    reg.registerAgent(a2);
    reg.registerAgent(a3);

    // Note: registerAgent sets state to Online, so we need to update a2 to Offline
    reg.updateState("a2", AgentState::Offline);

    auto all = reg.listAgents();
    EXPECT_EQ(all.size(), 3u);

    auto online = reg.listOnlineAgents();
    EXPECT_EQ(online.size(), 2u);
}

TEST(AgentRegistryTest, HeartbeatUpdatesTimestamp)
{
    AgentRegistry reg;
    AgentInfo info;
    info.agentId = "a-hb";
    info.hostname = "pc-hb";
    reg.registerAgent(info);

    auto before = reg.findAgent("a-hb").value().lastHeartbeat;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    reg.heartbeat("a-hb");
    auto after = reg.findAgent("a-hb").value().lastHeartbeat;

    EXPECT_GT(after, before);
}

TEST(AgentRegistryTest, QueueAndDequeueCommand)
{
    AgentRegistry reg;
    AgentInfo info; info.agentId = "a-cmd"; info.hostname = "pc-cmd";
    reg.registerAgent(info);

    AgentCommand cmd;
    cmd.commandId  = "cmd-001";
    cmd.agentId    = "a-cmd";
    cmd.featureUid = "lock-screen";
    cmd.operation  = "start";

    reg.queueCommand(cmd);
    auto pending = reg.dequeuePendingCommands("a-cmd");
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].commandId, "cmd-001");
    EXPECT_EQ(pending[0].featureUid, "lock-screen");

    // Dequeue again: should be empty
    auto empty = reg.dequeuePendingCommands("a-cmd");
    EXPECT_EQ(empty.size(), 0u);
}

TEST(AgentRegistryTest, ReportCommandResult)
{
    AgentRegistry reg;
    AgentInfo info; info.agentId = "a-res"; info.hostname = "pc-res";
    reg.registerAgent(info);

    AgentCommand cmd;
    cmd.commandId  = "cmd-res-001";
    cmd.agentId    = "a-res";
    cmd.featureUid = "screen-capture";
    cmd.operation  = "start";

    reg.queueCommand(cmd);
    reg.reportCommandResult("cmd-res-001", CommandStatus::Success, "{\"image\":\"base64...\"}", 150);

    auto found = reg.findCommand("cmd-res-001");
    ASSERT_TRUE(found.is_ok());
    EXPECT_EQ(found.value().status, CommandStatus::Success);
    EXPECT_EQ(found.value().durationMs, 150);
    EXPECT_FALSE(found.value().result.empty());
}

TEST(AgentRegistryTest, AgentCount)
{
    AgentRegistry reg;
    EXPECT_EQ(reg.agentCount(), 0u);

    AgentInfo a1; a1.agentId = "c1"; a1.hostname = "h1";
    AgentInfo a2; a2.agentId = "c2"; a2.hostname = "h2";
    reg.registerAgent(a1);
    reg.registerAgent(a2);
    EXPECT_EQ(reg.agentCount(), 2u);

    reg.unregisterAgent("c1");
    EXPECT_EQ(reg.agentCount(), 1u);
}
