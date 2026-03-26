# Hub32 Agent System — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a hub32 agent system where lightweight agents run on student computers, connect to the central hub32api server, receive commands (lock screen, show message, capture framebuffer, power control), execute them locally via Win32 API, and report results back — replacing the Veyon VNC-based approach with an HTTPS+WebSocket architecture.

**Architecture:** The agent is a Windows service (`hub32agent-service.exe`) that connects to hub32api via HTTPS. On startup it registers itself at `POST /api/v1/agents/register`, then opens a WebSocket at `/ws/agents/{id}` for real-time command dispatch. Commands arrive as JSON, get dispatched to feature handlers (ScreenCapture, ScreenLock, InputLock, MessageDisplay, PowerControl), and results are sent back over the same channel. The server adds an AgentRegistry to track online agents and an AgentPlugin that replaces mock data with live agent data.

**Tech Stack:** C++17, MinGW/GCC, cpp-httplib (agent HTTP client + server WS), nlohmann/json, spdlog, Win32 API (GDI for screen capture, CreateWindowEx for lock overlay, BlockInput, ExitWindowsEx), CMake 3.20+

---

## Scope

This plan covers **Phase 1 + Phase 2 + Phase 3** — everything needed for a working agent that can:
1. Register with the server
2. Receive and execute commands (lock screen, show message, capture framebuffer, power control, input lock)
3. Report results back in real-time
4. Heartbeat/health monitoring

**NOT in scope:** Screen broadcast (VNC reverse), file transfer, remote desktop, LDAP directory.

---

## File Map

### Server-side additions (hub32api)

| File | Responsibility |
|------|---------------|
| `include/hub32api/agent/AgentInfo.hpp` | AgentInfo struct + AgentState enum |
| `include/hub32api/agent/AgentCommand.hpp` | AgentCommand struct (command queue item) |
| `src/agent/AgentRegistry.hpp` | Thread-safe registry of connected agents |
| `src/agent/AgentRegistry.cpp` | Implementation |
| `src/api/v1/controllers/AgentController.hpp` | REST controller for agent endpoints |
| `src/api/v1/controllers/AgentController.cpp` | POST register, GET list, GET status, POST command |
| `src/api/v1/dto/AgentDto.hpp` | JSON DTOs for agent requests/responses |
| `src/server/WebSocketHandler.hpp` | WebSocket upgrade + message handler |
| `src/server/WebSocketHandler.cpp` | Agent WS connection management |
| `tests/unit/agent/test_agent_registry.cpp` | Unit tests for AgentRegistry |
| `tests/unit/agent/test_agent_controller.cpp` | Controller test stubs |

### Agent binary (hub32agent — new CMake target in same repo)

| File | Responsibility |
|------|---------------|
| `agent/CMakeLists.txt` | Agent build config |
| `agent/include/hub32agent/AgentConfig.hpp` | Agent config (server URL, key, poll interval) |
| `agent/include/hub32agent/AgentClient.hpp` | HTTP + WS client to server |
| `agent/include/hub32agent/CommandDispatcher.hpp` | Route commands to handlers |
| `agent/include/hub32agent/FeatureHandler.hpp` | Base interface for feature handlers |
| `agent/include/hub32agent/features/ScreenCapture.hpp` | GDI screen capture |
| `agent/include/hub32agent/features/ScreenLock.hpp` | Fullscreen overlay lock |
| `agent/include/hub32agent/features/InputLock.hpp` | BlockInput wrapper |
| `agent/include/hub32agent/features/MessageDisplay.hpp` | Popup message |
| `agent/include/hub32agent/features/PowerControl.hpp` | Shutdown/reboot |
| `agent/src/main.cpp` | Agent service entry point |
| `agent/src/AgentConfig.cpp` | Load config from JSON/Registry |
| `agent/src/AgentClient.cpp` | HTTPS registration + WS connection |
| `agent/src/CommandDispatcher.cpp` | Dispatch + result collection |
| `agent/src/features/ScreenCapture.cpp` | BitBlt + PNG encoding |
| `agent/src/features/ScreenLock.cpp` | Win32 overlay window |
| `agent/src/features/InputLock.cpp` | BlockInput(TRUE/FALSE) |
| `agent/src/features/MessageDisplay.cpp` | MessageBoxW or overlay |
| `agent/src/features/PowerControl.cpp` | ExitWindowsEx with privilege adjust |
| `agent/src/WinServiceAdapter.cpp` | Windows service wrapper (reuse pattern) |
| `agent/tests/test_command_dispatcher.cpp` | Unit test |
| `agent/tests/test_screen_capture.cpp` | Unit test |

---

## Task 1: Agent Data Types (Server-Side)

**Files:**
- Create: `include/hub32api/agent/AgentInfo.hpp`
- Create: `include/hub32api/agent/AgentCommand.hpp`
- Test: `tests/unit/agent/test_agent_registry.cpp`

- [ ] **Step 1: Create AgentInfo struct**

```cpp
// include/hub32api/agent/AgentInfo.hpp
#pragma once
#include <string>
#include <vector>
#include <chrono>
#include "hub32api/core/Types.hpp"
#include "hub32api/export.h"

namespace hub32api {

enum class AgentState { Offline, Online, Busy, Error };

HUB32API_EXPORT std::string to_string(AgentState s);

struct HUB32API_EXPORT AgentInfo
{
    Uid         agentId;
    std::string hostname;
    std::string ipAddress;
    uint16_t    agentPort  = 11082;
    std::string osVersion;
    std::string agentVersion;
    AgentState  state      = AgentState::Offline;
    Timestamp   registeredAt;
    Timestamp   lastHeartbeat;
    std::vector<std::string> capabilities; // ["screen-capture","lock-screen",...]
};

} // namespace hub32api
```

- [ ] **Step 2: Create AgentCommand struct**

```cpp
// include/hub32api/agent/AgentCommand.hpp
#pragma once
#include <string>
#include <map>
#include <chrono>
#include "hub32api/core/Types.hpp"
#include "hub32api/export.h"

namespace hub32api {

enum class CommandStatus { Pending, Running, Success, Failed, Timeout };

HUB32API_EXPORT std::string to_string(CommandStatus s);

struct HUB32API_EXPORT AgentCommand
{
    Uid         commandId;
    Uid         agentId;
    std::string featureUid;      // "lock-screen", "screen-capture", etc.
    std::string operation;       // "start", "stop"
    std::map<std::string, std::string> arguments;
    CommandStatus status = CommandStatus::Pending;
    Timestamp   createdAt;
    Timestamp   completedAt;
    std::string result;          // JSON result or error message
    int         durationMs = 0;
};

} // namespace hub32api
```

- [ ] **Step 3: Add to_string implementations**

Add `to_string(AgentState)`, `to_string(CommandStatus)` to `src/core/ApiContext.cpp` alongside existing `to_string` functions.

- [ ] **Step 4: Build to verify headers compile**

```bash
cd "c:/Users/Admin/Desktop/veyon/hub32api" && export PATH="/c/msys64/mingw64/bin:$PATH"
cmake --build --preset debug 2>&1 | grep -E "error:" | head -10
```
Expected: 0 errors

- [ ] **Step 5: Commit**

```bash
git add include/hub32api/agent/ src/core/ApiContext.cpp
git commit -m "feat(agent): add AgentInfo and AgentCommand data types"
```

---

## Task 2: AgentRegistry (Server-Side)

**Files:**
- Create: `src/agent/AgentRegistry.hpp`
- Create: `src/agent/AgentRegistry.cpp`
- Add to: `src/core/CMakeLists.txt` (source list)
- Test: `tests/unit/agent/test_agent_registry.cpp`

- [ ] **Step 1: Write the test**

```cpp
// tests/unit/agent/test_agent_registry.cpp
#include <gtest/gtest.h>
#include "agent/AgentRegistry.hpp"

using namespace hub32api;

TEST(AgentRegistryTest, RegisterAndFind)
{
    agent::AgentRegistry reg;
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
}

TEST(AgentRegistryTest, UnregisterRemovesAgent)
{
    agent::AgentRegistry reg;
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
    agent::AgentRegistry reg;

    AgentInfo a1; a1.agentId = "a1"; a1.hostname = "h1"; a1.state = AgentState::Online;
    AgentInfo a2; a2.agentId = "a2"; a2.hostname = "h2"; a2.state = AgentState::Offline;
    AgentInfo a3; a3.agentId = "a3"; a3.hostname = "h3"; a3.state = AgentState::Online;

    reg.registerAgent(a1);
    reg.registerAgent(a2);
    reg.registerAgent(a3);

    auto all = reg.listAgents();
    EXPECT_EQ(all.size(), 3);

    auto online = reg.listOnlineAgents();
    EXPECT_EQ(online.size(), 2);
}

TEST(AgentRegistryTest, HeartbeatUpdatesTimestamp)
{
    agent::AgentRegistry reg;
    AgentInfo info;
    info.agentId = "a-hb";
    info.hostname = "pc-hb";
    reg.registerAgent(info);

    auto before = reg.findAgent("a-hb").value().lastHeartbeat;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    reg.heartbeat("a-hb");
    auto after = reg.findAgent("a-hb").value().lastHeartbeat;

    EXPECT_GT(after, before);
}

TEST(AgentRegistryTest, QueueAndDequeueCommand)
{
    agent::AgentRegistry reg;
    AgentInfo info; info.agentId = "a-cmd"; info.hostname = "pc-cmd";
    reg.registerAgent(info);

    AgentCommand cmd;
    cmd.commandId  = "cmd-001";
    cmd.agentId    = "a-cmd";
    cmd.featureUid = "lock-screen";
    cmd.operation  = "start";

    reg.queueCommand(cmd);
    auto pending = reg.dequeuePendingCommands("a-cmd");
    ASSERT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].commandId, "cmd-001");

    // Dequeue again: should be empty
    auto empty = reg.dequeuePendingCommands("a-cmd");
    EXPECT_EQ(empty.size(), 0);
}
```

- [ ] **Step 2: Implement AgentRegistry**

```cpp
// src/agent/AgentRegistry.hpp
#pragma once
#include <mutex>
#include <unordered_map>
#include <vector>
#include <deque>
#include "hub32api/agent/AgentInfo.hpp"
#include "hub32api/agent/AgentCommand.hpp"
#include "hub32api/core/Result.hpp"
#include "hub32api/export.h"

namespace hub32api::agent {

class HUB32API_EXPORT AgentRegistry
{
public:
    Result<Uid>             registerAgent(const AgentInfo& info);
    void                    unregisterAgent(const Uid& agentId);
    Result<AgentInfo>       findAgent(const Uid& agentId) const;
    std::vector<AgentInfo>  listAgents() const;
    std::vector<AgentInfo>  listOnlineAgents() const;
    void                    heartbeat(const Uid& agentId);
    void                    updateState(const Uid& agentId, AgentState state);

    // Command queue per agent
    void                           queueCommand(const AgentCommand& cmd);
    std::vector<AgentCommand>      dequeuePendingCommands(const Uid& agentId);
    void                           reportCommandResult(const Uid& commandId,
                                                       CommandStatus status,
                                                       const std::string& result,
                                                       int durationMs);
    Result<AgentCommand>           findCommand(const Uid& commandId) const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<Uid, AgentInfo> m_agents;
    std::unordered_map<Uid, std::deque<AgentCommand>> m_commandQueues;
    std::unordered_map<Uid, AgentCommand> m_commandHistory; // commandId → cmd
};

} // namespace hub32api::agent
```

```cpp
// src/agent/AgentRegistry.cpp — full implementation
// - registerAgent: validate non-empty agentId, set registeredAt + lastHeartbeat to now,
//   state to Online, store in m_agents, return agentId
// - unregisterAgent: erase from m_agents and m_commandQueues
// - findAgent: lookup, return NotFound error if missing
// - listAgents: collect all values
// - listOnlineAgents: filter state == Online || Busy
// - heartbeat: find agent, update lastHeartbeat to now
// - updateState: find agent, set state
// - queueCommand: set createdAt, push to m_commandQueues[cmd.agentId], store in m_commandHistory
// - dequeuePendingCommands: swap out the deque for agentId, return as vector
// - reportCommandResult: find in m_commandHistory, update status/result/duration/completedAt
// - findCommand: lookup in m_commandHistory
// All methods lock m_mutex.
```

- [ ] **Step 3: Add to CMake**

Add `../agent/AgentRegistry.cpp` to `src/core/CMakeLists.txt` source list.
Add `test_agent_registry` to `tests/unit/CMakeLists.txt`.

- [ ] **Step 4: Run tests**

```bash
cmake --build --preset debug && ctest --test-dir build/debug -R test_agent_registry --output-on-failure
```
Expected: 5 tests PASS

- [ ] **Step 5: Commit**

```bash
git add src/agent/ tests/unit/agent/ src/core/CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "feat(agent): implement AgentRegistry with command queue"
```

---

## Task 3: Agent REST Endpoints (Server-Side)

**Files:**
- Create: `src/api/v1/controllers/AgentController.hpp`
- Create: `src/api/v1/controllers/AgentController.cpp`
- Create: `src/api/v1/dto/AgentDto.hpp`
- Modify: `src/server/Router.cpp` (add agent routes)
- Modify: `src/server/CMakeLists.txt` (add source)
- Modify: `src/server/HttpServer.cpp` (add AgentRegistry to Impl)
- Modify: `src/server/internal/Router.hpp` (add AgentRegistry to Services)

- [ ] **Step 1: Create AgentDto**

```cpp
// src/api/v1/dto/AgentDto.hpp
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace hub32api::api::v1::dto {

struct AgentRegisterRequest {
    std::string hostname;
    std::string agentKey;
    std::string osVersion;
    std::string agentVersion;
    std::vector<std::string> capabilities;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentRegisterRequest,
    hostname, agentKey, osVersion, agentVersion, capabilities)

struct AgentRegisterResponse {
    std::string agentId;
    std::string authToken; // JWT for subsequent requests
    int commandPollIntervalMs = 5000;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentRegisterResponse,
    agentId, authToken, commandPollIntervalMs)

struct AgentStatusDto {
    std::string agentId;
    std::string hostname;
    std::string ipAddress;
    std::string state;
    std::string agentVersion;
    std::string lastHeartbeat;
    std::vector<std::string> capabilities;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentStatusDto,
    agentId, hostname, ipAddress, state, agentVersion, lastHeartbeat, capabilities)

struct AgentCommandRequest {
    std::string featureUid;
    std::string operation;
    std::map<std::string, std::string> arguments;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandRequest,
    featureUid, operation, arguments)

struct AgentCommandResponse {
    std::string commandId;
    std::string status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandResponse, commandId, status)

struct AgentCommandResultRequest {
    std::string commandId;
    std::string status; // "success" or "failed"
    std::string result;
    int durationMs = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AgentCommandResultRequest,
    commandId, status, result, durationMs)

} // namespace hub32api::api::v1::dto
```

- [ ] **Step 2: Create AgentController**

Endpoints:
- `POST /api/v1/agents/register` — agent self-registration (public, validates agentKey)
- `DELETE /api/v1/agents/{id}` — agent unregister (protected)
- `GET /api/v1/agents` — list all agents (protected, admin only)
- `GET /api/v1/agents/{id}/status` — single agent status (protected)
- `POST /api/v1/agents/{id}/commands` — push command to agent (protected)
- `GET /api/v1/agents/{id}/commands` — agent polls for pending commands (agent auth)
- `PUT /api/v1/agents/{id}/commands/{cid}` — agent reports command result (agent auth)
- `POST /api/v1/agents/{id}/heartbeat` — agent heartbeat (agent auth)

- [ ] **Step 3: Register routes in Router**

Add `registerAgentRoutes()` private method. Add `agent::AgentRegistry& agentRegistry` to `Router::Services`.

- [ ] **Step 4: Wire AgentRegistry into HttpServer**

Add `std::unique_ptr<agent::AgentRegistry> agentRegistry` to `HttpServer::Impl`. Create in constructor. Pass to Router::Services.

- [ ] **Step 5: Update OpenAPI spec in Router.cpp**

Add all 8 agent endpoints to `buildOpenApiSpec()`.

- [ ] **Step 6: Build and test manually**

```bash
cmake --build --preset debug
# Start server, test:
curl -s -X POST http://127.0.0.1:11081/api/v1/agents/register \
  -H "Content-Type: application/json" \
  -d '{"hostname":"PC-Lab-01","agentKey":"test","osVersion":"Win10","agentVersion":"1.0.0","capabilities":["screen-capture","lock-screen"]}'
```
Expected: 200 with `{agentId, authToken, commandPollIntervalMs}`

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "feat(agent): add agent REST endpoints and registration"
```

---

## Task 4: Agent Binary — Skeleton + Config

**Files:**
- Create: `agent/CMakeLists.txt`
- Create: `agent/include/hub32agent/AgentConfig.hpp`
- Create: `agent/src/AgentConfig.cpp`
- Create: `agent/src/main.cpp`
- Create: `agent/src/WinServiceAdapter.cpp` / `.hpp`
- Modify: `CMakeLists.txt` (root — add_subdirectory(agent))

- [ ] **Step 1: Create agent CMakeLists.txt**

Fetches cpp-httplib (already available from parent). Links nlohmann_json, spdlog. Produces `hub32agent-service.exe`.

- [ ] **Step 2: Create AgentConfig**

```cpp
struct AgentConfig {
    std::string serverUrl = "http://127.0.0.1:11081";
    std::string agentKey;
    int pollIntervalMs = 5000;
    int heartbeatIntervalMs = 30000;
    std::string logLevel = "info";
    std::string logFile;

    static AgentConfig from_file(const std::string& path);
    static AgentConfig from_registry();
    static AgentConfig defaults();
};
```

- [ ] **Step 3: Create main.cpp**

Service entry with `--console`, `--install`, `--uninstall`, `--config` flags. Reuse WinServiceAdapter pattern from hub32api.

- [ ] **Step 4: Build agent skeleton**

```bash
cmake --preset debug && cmake --build --preset debug --target hub32agent-service
```
Expected: `hub32agent-service.exe` produced in `build/debug/bin/`

- [ ] **Step 5: Commit**

```bash
git add agent/ CMakeLists.txt
git commit -m "feat(agent): agent binary skeleton with config and service adapter"
```

---

## Task 5: AgentClient — Server Connection

**Files:**
- Create: `agent/include/hub32agent/AgentClient.hpp`
- Create: `agent/src/AgentClient.cpp`
- Test: `agent/tests/test_agent_client.cpp`

- [ ] **Step 1: Implement AgentClient**

```cpp
class AgentClient {
public:
    explicit AgentClient(const AgentConfig& cfg);

    // Registration
    Result<RegisterResponse> registerWithServer();
    void unregister();

    // Command polling (HTTP fallback)
    std::vector<AgentCommand> pollCommands();
    void reportResult(const std::string& commandId, const std::string& status,
                      const std::string& result, int durationMs);

    // Heartbeat
    void sendHeartbeat();

    // State
    bool isRegistered() const;
    const std::string& agentId() const;

private:
    AgentConfig m_cfg;
    std::string m_agentId;
    std::string m_authToken;
    std::unique_ptr<httplib::Client> m_client;
};
```

Uses `httplib::Client` for HTTPS to server. Sets `Authorization: Bearer {authToken}` on all requests after registration.

- [ ] **Step 2: Build and test**

Start hub32api server, run agent in console mode → should register and begin polling.

- [ ] **Step 3: Commit**

```bash
git add agent/ && git commit -m "feat(agent): implement AgentClient with registration and polling"
```

---

## Task 6: CommandDispatcher + FeatureHandler Interface

**Files:**
- Create: `agent/include/hub32agent/FeatureHandler.hpp`
- Create: `agent/include/hub32agent/CommandDispatcher.hpp`
- Create: `agent/src/CommandDispatcher.cpp`
- Test: `agent/tests/test_command_dispatcher.cpp`

- [ ] **Step 1: Create FeatureHandler interface**

```cpp
class FeatureHandler {
public:
    virtual ~FeatureHandler() = default;
    virtual std::string featureUid() const = 0;
    virtual std::string name() const = 0;
    virtual Result<std::string> execute(const std::string& operation,
                                        const std::map<std::string,std::string>& args) = 0;
};
```

- [ ] **Step 2: Implement CommandDispatcher**

```cpp
class CommandDispatcher {
public:
    void registerHandler(std::unique_ptr<FeatureHandler> handler);
    Result<std::string> dispatch(const AgentCommand& cmd);
    std::vector<std::string> registeredFeatures() const;
private:
    std::unordered_map<std::string, std::unique_ptr<FeatureHandler>> m_handlers;
};
```

- [ ] **Step 3: Write test**

Register a mock handler, dispatch a command, verify result.

- [ ] **Step 4: Build and run test**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(agent): implement CommandDispatcher and FeatureHandler interface"
```

---

## Task 7: Feature — Screen Capture (Win32 GDI)

**Files:**
- Create: `agent/include/hub32agent/features/ScreenCapture.hpp`
- Create: `agent/src/features/ScreenCapture.cpp`
- Test: `agent/tests/test_screen_capture.cpp`

- [ ] **Step 1: Implement ScreenCapture**

```cpp
class ScreenCapture : public FeatureHandler {
public:
    std::string featureUid() const override { return "screen-capture"; }
    std::string name() const override { return "Screen Capture"; }
    Result<std::string> execute(const std::string& operation,
                                const Args& args) override;
private:
    // Capture using BitBlt + CreateCompatibleBitmap
    // Encode to PNG using minimal PNG writer (or stb_image_write)
    // Return base64-encoded image string
    std::vector<uint8_t> captureScreen(int width, int height);
    std::vector<uint8_t> encodePng(const std::vector<uint8_t>& bmpData, int w, int h);
};
```

Win32 API calls:
- `GetDC(nullptr)` — get screen DC
- `CreateCompatibleDC` + `CreateCompatibleBitmap`
- `BitBlt(memDC, 0, 0, w, h, screenDC, 0, 0, SRCCOPY)`
- `GetDIBits` → raw pixel data
- Encode to BMP format (simple header + raw data), return as bytes

For MVP: return raw BMP bytes. PNG encoding can be added later with stb_image_write.

- [ ] **Step 2: Test**

Capture should produce non-empty byte array with valid BMP header.

- [ ] **Step 3: Commit**

```bash
git commit -m "feat(agent): implement ScreenCapture feature with Win32 GDI"
```

---

## Task 8: Feature — Screen Lock (Win32 Overlay)

**Files:**
- Create: `agent/include/hub32agent/features/ScreenLock.hpp`
- Create: `agent/src/features/ScreenLock.cpp`

- [ ] **Step 1: Implement ScreenLock**

On "start":
- Create a `WNDCLASS` with black background
- `CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, ..., WS_POPUP, 0, 0, screenW, screenH, ...)`
- Set window to cover all monitors: `GetSystemMetrics(SM_XVIRTUALSCREEN)` etc.
- `ShowWindow(hwnd, SW_SHOW)` + `SetForegroundWindow`
- Optional: `BlockInput(TRUE)` if running with admin privileges
- Store hwnd in member variable

On "stop":
- `DestroyWindow(m_lockWindow)`
- `BlockInput(FALSE)` if it was enabled
- Clear hwnd

Thread safety: Lock window runs on a separate message loop thread.

- [ ] **Step 2: Commit**

```bash
git commit -m "feat(agent): implement ScreenLock feature with Win32 overlay"
```

---

## Task 9: Feature — Input Lock, Message Display, Power Control

**Files:**
- Create: `agent/include/hub32agent/features/InputLock.hpp`
- Create: `agent/src/features/InputLock.cpp`
- Create: `agent/include/hub32agent/features/MessageDisplay.hpp`
- Create: `agent/src/features/MessageDisplay.cpp`
- Create: `agent/include/hub32agent/features/PowerControl.hpp`
- Create: `agent/src/features/PowerControl.cpp`

- [ ] **Step 1: InputLock** — `BlockInput(TRUE)` on start, `BlockInput(FALSE)` on stop. Requires admin.

- [ ] **Step 2: MessageDisplay** — Parse `args["text"]` and `args["title"]`. Show with `MessageBoxW` on a separate thread (non-blocking). On stop: find and close the message window.

- [ ] **Step 3: PowerControl** — Parse `args["action"]`: "shutdown", "reboot", "logoff".
- Enable `SE_SHUTDOWN_NAME` privilege via `AdjustTokenPrivileges`
- Call `ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, ...)` or `EWX_REBOOT` or `EWX_LOGOFF`
- Return success/failure

- [ ] **Step 4: Build all features**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(agent): implement InputLock, MessageDisplay, PowerControl features"
```

---

## Task 10: Agent Main Loop Integration

**Files:**
- Modify: `agent/src/main.cpp`
- Modify: `agent/src/AgentClient.cpp`

- [ ] **Step 1: Wire everything together in main()**

```
1. Load AgentConfig
2. Create CommandDispatcher
3. Register all 5 feature handlers
4. Create AgentClient(config)
5. Register with server → get agentId + authToken
6. Enter main loop:
   a. Poll commands from server (HTTP GET /api/v1/agents/{id}/commands)
   b. For each command: dispatch to handler
   c. Report results back (HTTP PUT /api/v1/agents/{id}/commands/{cid})
   d. Send heartbeat every heartbeatInterval
   e. Sleep pollInterval
   f. Check for stop signal
```

- [ ] **Step 2: Add graceful shutdown**

Use the same `volatile sig_atomic_t` + watcher thread pattern from hub32api.

- [ ] **Step 3: End-to-end test**

Start hub32api server. Start hub32agent in console mode. Verify:
1. Agent registers (check `GET /api/v1/agents`)
2. Send lock-screen command (POST to server)
3. Agent receives and executes it
4. Result appears in server

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(agent): complete agent main loop with all features integrated"
```

---

## Task 11: Server Plugin Update — Route Commands to Live Agents

**Files:**
- Modify: `src/plugins/computer/ComputerPlugin.cpp`
- Modify: `src/plugins/feature/FeaturePlugin.cpp`
- Modify: `src/server/HttpServer.cpp`

- [ ] **Step 1: Update ComputerPlugin**

When AgentRegistry has online agents, `listComputers()` should return live agent data instead of mock data. Check `agentRegistry.listOnlineAgents()` first; if empty, fall back to mock.

- [ ] **Step 2: Update FeaturePlugin**

`controlFeature(computerUid, featureUid, op, args)`:
- Find agent by computerUid (agentId matches or hostname matches)
- If online agent found: queue command via AgentRegistry
- If no agent: use existing mock behavior

- [ ] **Step 3: Pass AgentRegistry to plugins**

Add AgentRegistry reference to PluginRegistry or pass directly to plugins.

- [ ] **Step 4: Test full flow**

Teacher → `PUT /api/v1/computers/pc-001/features/lock-screen {active:true}`
→ Server finds agent-001 for pc-001
→ Queues lock-screen command
→ Agent polls, receives, executes
→ Agent reports success
→ Server returns 200 to teacher

- [ ] **Step 5: Commit**

```bash
git commit -m "feat: route feature commands to live agents instead of mock data"
```

---

## Task 12: Final Build, Full Test Suite, Documentation

- [ ] **Step 1: Run full build**

```bash
cd "c:/Users/Admin/Desktop/veyon/hub32api"
cmake --preset debug && cmake --build --preset debug
```

- [ ] **Step 2: Run all tests**

```bash
ctest --test-dir build/debug --output-on-failure
```
Expected: All tests PASS

- [ ] **Step 3: End-to-end smoke test**

```bash
# Terminal 1: Start server
./build/debug/bin/hub32api-service.exe --console

# Terminal 2: Start agent
./build/debug/bin/hub32agent-service.exe --console --config agent/conf/default.json

# Terminal 3: Test
curl -s http://127.0.0.1:11081/api/v1/agents | python3 -m json.tool
# Should show agent-001 online

TOKEN=$(curl -s -X POST ... | jq -r .token)
curl -s -X POST http://127.0.0.1:11081/api/v1/agents/agent-001/commands \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"featureUid":"lock-screen","operation":"start"}'
# Agent should lock screen
```

- [ ] **Step 4: Update OpenAPI spec**

Ensure all 8 agent endpoints are in `/openapi.json`.

- [ ] **Step 5: Final commit**

```bash
git add -A && git commit -m "feat: hub32 agent system complete — 5 features, registration, command queue"
```
