# Phase 2: Agent ↔ Server Communication Enhancement — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enhance the existing agent↔server communication to integrate with the new database model. Agent registration now creates/updates a real computer record in school.db. Heartbeats update computer state. Feature commands flow through the database-backed model with proper location awareness.

**Architecture:** Agent registers with MAC+hostname → server matches or creates computer in ComputerRepository. Heartbeats update `computers.last_heartbeat` and `computers.state`. Offline detection via heartbeat timeout (3x interval = 90s). Commands go through AgentRegistry (in-memory queue) but results are persisted.

**Tech Stack:** C++17, SQLite3, existing AgentRegistry + AgentController

**Depends on:** Phase 1 (database model exists)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `src/api/v1/controllers/AgentController.cpp` | Registration → ComputerRepository upsert |
| Modify | `src/api/v1/controllers/AgentController.hpp` | Add ComputerRepository dependency |
| Modify | `src/agent/AgentRegistry.cpp` | Heartbeat timeout detection thread |
| Modify | `src/agent/AgentRegistry.hpp` | Add timeout detection, callback for state change |
| Create | `src/agent/HeartbeatMonitor.hpp` | Background thread monitoring agent heartbeats |
| Create | `src/agent/HeartbeatMonitor.cpp` | Implementation |
| Modify | `src/server/HttpServer.cpp` | Wire ComputerRepository into AgentController |
| Modify | `src/server/Router.cpp` | Pass repos to agent route handlers |
| Create | `tests/unit/agent/test_heartbeat_monitor.cpp` | Test timeout detection |
| Create | `tests/integration/test_agent_registration_flow.cpp` | Full flow test |

---

### Task 1: HeartbeatMonitor

**Files:**
- Create: `src/agent/HeartbeatMonitor.hpp`
- Create: `src/agent/HeartbeatMonitor.cpp`
- Create: `tests/unit/agent/test_heartbeat_monitor.cpp`

**Why:** Currently no mechanism detects offline agents. The spec requires: if 3x heartbeat interval (90s) passes without heartbeat, mark agent as offline and update `computers.state = 'offline'` in the database.

- [ ] **Step 1: Write failing test**

```cpp
// tests/unit/agent/test_heartbeat_monitor.cpp
#include <gtest/gtest.h>
#include "agent/HeartbeatMonitor.hpp"
#include "agent/AgentRegistry.hpp"
#include "hub32api/agent/AgentInfo.hpp"
#include <thread>
#include <chrono>

using namespace hub32api::agent;

TEST(HeartbeatMonitor, DetectsOfflineAgent)
{
    AgentRegistry registry;
    std::vector<Uid> offlineAgents;

    HeartbeatMonitor monitor(registry, std::chrono::seconds(1),
        [&](const Uid& agentId) {
            offlineAgents.push_back(agentId);
        });

    // Register agent
    AgentInfo info;
    info.agentId = "agent-001";
    info.hostname = "PC-01";
    info.state = AgentState::Online;
    registry.registerAgent(info);

    // Start monitor with 1s check interval, 2s timeout
    monitor.start(std::chrono::seconds(2));

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::seconds(3));
    monitor.stop();

    EXPECT_FALSE(offlineAgents.empty());
    EXPECT_EQ(offlineAgents.front(), "agent-001");
}
```

- [ ] **Step 2: Write HeartbeatMonitor header**

```cpp
// src/agent/HeartbeatMonitor.hpp
#pragma once

#include <chrono>
#include <functional>
#include <thread>
#include <atomic>
#include "hub32api/core/Types.hpp"

namespace hub32api::agent {

class AgentRegistry;

class HeartbeatMonitor
{
public:
    using OfflineCallback = std::function<void(const Uid& agentId)>;

    HeartbeatMonitor(AgentRegistry& registry,
                     std::chrono::seconds checkInterval,
                     OfflineCallback onOffline);
    ~HeartbeatMonitor();

    void start(std::chrono::seconds timeout);
    void stop();

private:
    void monitorLoop();

    AgentRegistry& m_registry;
    std::chrono::seconds m_checkInterval;
    std::chrono::seconds m_timeout{90};
    OfflineCallback m_onOffline;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace hub32api::agent
```

- [ ] **Step 3: Write HeartbeatMonitor implementation**

Background thread checks all agents every `checkInterval`. For each agent where `now - lastHeartbeat > timeout`, calls `m_onOffline(agentId)` and updates state to Offline.

- [ ] **Step 4: Build and run tests**
- [ ] **Step 5: Commit**

---

### Task 2: Agent Registration → ComputerRepository Integration

**Files:**
- Modify: `src/api/v1/controllers/AgentController.cpp`
- Modify: `src/api/v1/controllers/AgentController.hpp`

**Why:** Currently agent registration only adds to in-memory AgentRegistry. It must also upsert into `computers` table via ComputerRepository.

- [ ] **Step 1: Add ComputerRepository reference to AgentController**

```cpp
// AgentController.hpp
class AgentController {
public:
    AgentController(agent::AgentRegistry& registry,
                    const std::string& agentKeyHash,
                    db::ComputerRepository& computerRepo);
    // ...
private:
    db::ComputerRepository& m_computerRepo;
};
```

- [ ] **Step 2: In handleRegister(), upsert computer after agent registration**

```cpp
// In handleRegister():
// 1. Register in AgentRegistry (existing)
// 2. Upsert in ComputerRepository:
auto existing = m_computerRepo.findByHostname(req_dto.hostname);
if (existing.is_ok()) {
    // Update existing computer
    m_computerRepo.updateHeartbeat(existing.value().id, req_dto.ipAddress, req_dto.agentVersion);
    m_computerRepo.updateState(existing.value().id, "online");
} else {
    // Create new computer in the "unassigned" location.
    // NOTE: DatabaseManager::createSchema() must create a sentinel location:
    //   INSERT OR IGNORE INTO locations (id, school_id, name, type)
    //     VALUES ('unassigned', (SELECT id FROM schools LIMIT 1), 'Unassigned', 'unassigned');
    // Or alternatively, make computers.location_id nullable in the schema.
    // Recommended approach: make location_id nullable (NULL = not yet assigned to a room).
    m_computerRepo.createUnassigned(req_dto.hostname, req_dto.macAddress);
}
```

> **IMPORTANT:** The `computers.location_id` column must be made nullable (`TEXT REFERENCES locations(id)` without `NOT NULL`) in the Phase 1 DatabaseManager schema. This avoids FK violations when agents register before being assigned to a room. Admin assigns computers to locations via the dashboard.

- [ ] **Step 3: In handleHeartbeat(), update computer state and timestamp**

```cpp
// In handleHeartbeat():
// 1. Update AgentRegistry (existing)
// 2. Update ComputerRepository:
auto computer = m_computerRepo.findByHostname(agentInfo.hostname);
if (computer.is_ok()) {
    m_computerRepo.updateHeartbeat(computer.value().id, agentInfo.ipAddress, agentInfo.agentVersion);
}
```

- [ ] **Step 4: Wire HeartbeatMonitor offline callback to ComputerRepository**

In `HttpServer.cpp`, when creating HeartbeatMonitor:
```cpp
m_impl->heartbeatMonitor = std::make_unique<agent::HeartbeatMonitor>(
    *m_impl->agentRegistry,
    std::chrono::seconds(30),
    [&](const Uid& agentId) {
        auto agent = m_impl->agentRegistry->findAgent(agentId);
        if (agent.is_ok()) {
            m_impl->computerRepo->updateStateByHostname(agent.value().hostname, "offline");
        }
        m_impl->agentRegistry->updateState(agentId, AgentState::Offline);
        spdlog::warn("[HeartbeatMonitor] agent {} went offline", agentId);
    });
m_impl->heartbeatMonitor->start(std::chrono::seconds(90));
```

- [ ] **Step 5: Build and run tests**
- [ ] **Step 6: Commit**

---

### Task 3: Feature Command Integration with Location Awareness

**Files:**
- Modify: `src/plugins/feature/FeaturePlugin.cpp`

**Why:** Feature commands should be location-aware. A teacher can only send commands to computers in their assigned locations.

- [ ] **Step 1: Add location check before queuing commands**

In `FeaturePlugin::controlFeature()`, before queuing:
```cpp
// If we have repo access, verify computer belongs to a location
// the requesting teacher has access to.
// (This check is done at the controller/middleware level, not here)
```

Actually, the authorization check belongs in the controller or middleware, not in the plugin. The plugin should just execute. The role-based access check happens in the route handler.

- [ ] **Step 2: Add role-based access check in FeatureController route handlers**

In Router.cpp, for feature routes:
```cpp
// Before executing feature:
// 1. Get teacher ID from JWT
// 2. Get computer's location_id from ComputerRepository
// 3. Check TeacherLocationRepository.hasAccess(teacherId, locationId)
// 4. If admin role, skip check
```

- [ ] **Step 3: Write integration test**

```cpp
// tests/integration/test_agent_registration_flow.cpp
// Test: agent registers → appears in /api/v1/computers →
// teacher can send lock → agent receives command
```

- [ ] **Step 4: Build and run tests**
- [ ] **Step 5: Commit**

---

### Task 4: End-to-End Integration Test

**Files:**
- Modify: `tests/integration/test_api_v1_full.cpp`

- [ ] **Step 1: Write integration test for full agent lifecycle**

```
1. POST /api/v1/auth (login as admin) → get token
2. POST /api/v1/schools (create school) → school_id
3. POST /api/v1/locations (create room in school) → location_id
4. POST /api/v1/agents/register (agent registers) → agent in registry + computer in DB
5. GET /api/v1/locations/:id/computers → see the agent's computer
6. PUT /api/v1/computers/:id/features/:fid (start lock) → command queued
7. GET /api/v1/agents/:id/commands (agent polls) → get lock command
8. PUT /api/v1/agents/:id/commands/:cid (agent reports result)
9. [Wait 90s or simulate timeout] → agent goes offline
10. GET /api/v1/locations/:id/computers → computer state = offline
```

- [ ] **Step 2: Build and run**
- [ ] **Step 3: Commit**

---

## Phase 2 Completion Checklist

- [ ] HeartbeatMonitor detects offline agents (3x interval timeout)
- [ ] Agent registration creates/updates computer in school.db
- [ ] Agent heartbeat updates computer state and timestamp
- [ ] Offline agents have computers.state set to 'offline'
- [ ] Feature commands respect location-based access control
- [ ] End-to-end integration test passes
- [ ] All existing tests still pass
