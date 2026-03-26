# Phase 3: mediasoup C++ SFU Integration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Embed the mediasoup C++ worker into hub32api-server as a static library. Create a MediasoupManager that spawns worker threads and communicates via FlatBuffers. Create a RoomManager that maps each location to a mediasoup Router. Implement WebRTC signaling REST API for transport creation, DTLS connect, produce, and consume.

**Architecture:** mediasoup worker runs as embedded threads (1 per CPU core). Communication via ChannelReadFn/ChannelWriteFn callbacks using FlatBuffers serialization. Each location (room) gets one Router. Students produce H.264 streams, teachers consume them. Simulcast 3 layers supported.

**Tech Stack:** C++17, mediasoup (C++ worker, FlatBuffers), Meson (for mediasoup build), CMake (for integration), libsrtp2, libuv

**Depends on:** Phase 1 (location/room model exists)

**Reference implementation:** github.com/ouxianghui/mediasoup-server

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `third_party/mediasoup/` | Git submodule: mediasoup worker source |
| Create | `cmake/deps/FindMediasoup.cmake` | Build mediasoup worker as static lib |
| Create | `src/media/MediasoupManager.hpp` | Manage worker lifecycle, channel communication |
| Create | `src/media/MediasoupManager.cpp` | Implementation |
| Create | `src/media/RoomManager.hpp` | 1 Router per location, on-demand |
| Create | `src/media/RoomManager.cpp` | Implementation |
| Create | `src/media/TransportManager.hpp` | Create/track WebRtcTransports |
| Create | `src/media/TransportManager.cpp` | Implementation |
| Create | `src/media/FlatBuffersCodec.hpp` | Serialize/deserialize mediasoup messages |
| Create | `src/media/FlatBuffersCodec.cpp` | Implementation |
| Create | `src/media/CMakeLists.txt` | Build media module |
| Create | `src/api/v1/controllers/StreamController.hpp` | WebRTC signaling endpoints |
| Create | `src/api/v1/controllers/StreamController.cpp` | Implementation |
| Create | `src/api/v1/dto/StreamDto.hpp` | Stream/Transport DTOs |
| Modify | `src/server/HttpServer.cpp` | Create MediasoupManager, RoomManager |
| Modify | `src/server/Router.cpp` | Register /api/v1/stream/* routes |
| Modify | `src/server/internal/Router.hpp` | Add media refs to Services |
| Modify | `CMakeLists.txt` | Add mediasoup submodule build |
| Create | `tests/unit/media/test_mediasoup_manager.cpp` | Test worker lifecycle |
| Create | `tests/unit/media/test_room_manager.cpp` | Test router creation |

---

### Task 0: mediasoup Windows/MinGW Feasibility Check

**Why:** mediasoup's C++ worker is primarily developed for Linux/macOS. Building on MSYS2/MinGW64 is not officially supported and may require patching. This task MUST be completed before writing any integration code.

- [ ] **Step 1: Research mediasoup build requirements**

Check mediasoup worker's Meson build for platform restrictions. Check if libuv, libsrtp2, usrsctp, abseil build on MinGW.

```bash
# Check if Meson is available
pacman -S meson ninja
# Check libuv availability
pacman -Ss libuv
# Check libsrtp
pacman -Ss libsrtp
```

- [ ] **Step 2: Try building mediasoup worker on MSYS2/MinGW64**

```bash
git clone https://github.com/versatica/mediasoup.git /tmp/mediasoup-test
cd /tmp/mediasoup-test/worker
meson setup builddir --default-library=static
ninja -C builddir
```

- [ ] **Step 3: Document results**

If build succeeds: proceed to Task 1.
If build fails:
- **Fallback A:** Cross-compile mediasoup worker on Linux, deploy as a separate binary alongside hub32api
- **Fallback B:** Run mediasoup as a Node.js sidecar process on the VPS (hub32api communicates via HTTP/WebSocket)
- **Fallback C:** Use the ouxianghui/mediasoup-server C++ reference as the starting point (already proven buildable)

Document the chosen approach before proceeding.

- [ ] **Step 4: If using Fallback B (Node.js sidecar), create `deploy/mediasoup-worker/` directory**

The VPS runs Linux, so the native mediasoup worker will build there even if it doesn't build on Windows for development. The Windows dev environment only needs the REST signaling API — the actual SFU runs on the VPS.

---

### Task 1: Add mediasoup as Git Submodule

- [ ] **Step 1: Clone mediasoup worker source**

```bash
cd c:/Users/Admin/Desktop/veyon/hub32api
git submodule add https://github.com/versatica/mediasoup.git third_party/mediasoup
cd third_party/mediasoup
git checkout v3.14.x  # or latest stable
```

- [ ] **Step 2: Verify mediasoup worker directory structure**

```
third_party/mediasoup/worker/
├── meson.build
├── src/
│   ├── Channel/
│   ├── RTC/
│   ├── MediaSoupErrors.hpp
│   └── Worker.cpp
├── include/
│   ├── MediaSoupClientError.hpp
│   └── ...
├── fbs/           # FlatBuffers schemas
└── subprojects/   # libuv, openssl, libsrtp2, usrsctp, abseil
```

- [ ] **Step 3: Build mediasoup worker standalone to verify it compiles**

```bash
cd third_party/mediasoup/worker
meson setup builddir --default-library=static
ninja -C builddir
```

- [ ] **Step 4: Commit submodule**

```bash
git add third_party/mediasoup .gitmodules
git commit -m "chore: add mediasoup v3.14 as git submodule"
```

---

### Task 2: CMake Integration for mediasoup

**Files:**
- Create: `cmake/deps/FindMediasoup.cmake`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create FindMediasoup.cmake**

Use CMake's ExternalProject or FetchContent with Meson as build system:
```cmake
# cmake/deps/FindMediasoup.cmake
include(ExternalProject)

set(MEDIASOUP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/mediasoup/worker")
set(MEDIASOUP_BUILD_DIR "${CMAKE_BINARY_DIR}/mediasoup-build")

ExternalProject_Add(mediasoup_worker
    SOURCE_DIR ${MEDIASOUP_SOURCE_DIR}
    BINARY_DIR ${MEDIASOUP_BUILD_DIR}
    CONFIGURE_COMMAND meson setup ${MEDIASOUP_BUILD_DIR} ${MEDIASOUP_SOURCE_DIR}
        --default-library=static
        --buildtype=release
    BUILD_COMMAND ninja -C ${MEDIASOUP_BUILD_DIR}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${MEDIASOUP_BUILD_DIR}/libmediasoup-worker.a
)

add_library(mediasoup::worker STATIC IMPORTED GLOBAL)
set_target_properties(mediasoup::worker PROPERTIES
    IMPORTED_LOCATION ${MEDIASOUP_BUILD_DIR}/libmediasoup-worker.a
    INTERFACE_INCLUDE_DIRECTORIES "${MEDIASOUP_SOURCE_DIR}/include"
)
add_dependencies(mediasoup::worker mediasoup_worker)
```

- [ ] **Step 2: Link mediasoup in CMakeLists.txt**
- [ ] **Step 3: Build to verify linkage**
- [ ] **Step 4: Commit**

---

### Task 3: MediasoupManager

**Files:**
- Create: `src/media/MediasoupManager.hpp`
- Create: `src/media/MediasoupManager.cpp`
- Create: `src/media/CMakeLists.txt`

- [ ] **Step 1: Write MediasoupManager header**

```cpp
// src/media/MediasoupManager.hpp
#pragma once

#include <memory>
#include <vector>
#include <string>

namespace hub32api::media {

class MediasoupManager
{
public:
    struct Config {
        int numWorkers = 0; // 0 = auto (CPU cores)
        std::string logLevel = "warn";
    };

    explicit MediasoupManager(const Config& cfg);
    ~MediasoupManager();

    MediasoupManager(const MediasoupManager&) = delete;
    MediasoupManager& operator=(const MediasoupManager&) = delete;

    bool isRunning() const noexcept;
    int workerCount() const noexcept;

    /// Create a Router on the least-loaded worker
    /// Returns router ID
    std::string createRouter(const std::string& mediaCodecs);

    /// Destroy a Router
    void closeRouter(const std::string& routerId);

    /// Create a WebRtcTransport on a Router
    /// Returns transport ID + ICE candidates + DTLS fingerprints
    struct TransportInfo {
        std::string id;
        std::string iceCandidates;   // JSON array
        std::string iceParameters;   // JSON object
        std::string dtlsParameters;  // JSON object
    };
    TransportInfo createWebRtcTransport(const std::string& routerId,
                                         const std::string& listenIps);

    /// Connect a transport (DTLS)
    void connectTransport(const std::string& transportId,
                          const std::string& dtlsParameters);

    /// Create a Producer
    struct ProducerInfo {
        std::string id;
        std::string kind; // "video" or "audio"
    };
    ProducerInfo produce(const std::string& transportId,
                         const std::string& kind,
                         const std::string& rtpParameters);

    /// Create a Consumer
    struct ConsumerInfo {
        std::string id;
        std::string producerId;
        std::string kind;
        std::string rtpParameters;
    };
    ConsumerInfo consume(const std::string& transportId,
                         const std::string& producerId,
                         const std::string& rtpCapabilities);

    /// Close a transport
    void closeTransport(const std::string& transportId);

    /// Get RTP capabilities of a Router
    std::string getRouterRtpCapabilities(const std::string& routerId);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hub32api::media
```

- [ ] **Step 2: Write implementation using mediasoup worker API**

The implementation will:
1. Spawn N worker threads via `mediasoup_worker_run()`
2. Communicate via ChannelReadFn/ChannelWriteFn callbacks
3. Serialize commands as FlatBuffers
4. Parse responses from FlatBuffers
5. Maintain a map of routerId → workerId for load balancing

- [ ] **Step 3: Write basic test**
- [ ] **Step 4: Build and verify**
- [ ] **Step 5: Commit**

---

### Task 4: RoomManager

**Files:**
- Create: `src/media/RoomManager.hpp`
- Create: `src/media/RoomManager.cpp`

- [ ] **Step 1: Write RoomManager header**

```cpp
// src/media/RoomManager.hpp
#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace hub32api::media {

class MediasoupManager;

class RoomManager
{
public:
    explicit RoomManager(MediasoupManager& manager);
    ~RoomManager();

    /// Get or create a Router for a location (room)
    std::string getOrCreateRouter(const std::string& locationId);

    /// Destroy a room's Router (when all agents disconnect)
    void destroyRoom(const std::string& locationId);

    /// Get Router RTP capabilities for a location
    std::string getRtpCapabilities(const std::string& locationId);

    /// Check if a room has an active Router
    bool hasRoom(const std::string& locationId) const;

    /// Get count of active rooms
    size_t roomCount() const;

private:
    MediasoupManager& m_manager;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_locationToRouter; // locationId → routerId
};
```

- [ ] **Step 2: Write implementation**
- [ ] **Step 3: Write test**
- [ ] **Step 4: Commit**

---

### Task 5: StreamController (WebRTC Signaling API)

**Files:**
- Create: `src/api/v1/controllers/StreamController.hpp`
- Create: `src/api/v1/controllers/StreamController.cpp`
- Create: `src/api/v1/dto/StreamDto.hpp`
- Modify: `src/server/Router.cpp`
- Modify: `locales/en.json`, `vi.json`, `zh_CN.json`

**Endpoints:**
```
POST   /api/v1/stream/transport              Create WebRtcTransport
POST   /api/v1/stream/transport/:id/connect  DTLS connect
POST   /api/v1/stream/produce                Create Producer
POST   /api/v1/stream/consume                Create Consumer
DELETE /api/v1/stream/transport/:id           Cleanup transport
GET    /api/v1/stream/ice-servers            Return STUN+TURN URLs
GET    /api/v1/stream/capabilities/:locationId  Router RTP capabilities
```

- [ ] **Step 1: Write StreamDto.hpp**

```cpp
struct CreateTransportRequest {
    std::string locationId;
    std::string direction; // "send" or "recv"
};

struct CreateTransportResponse {
    std::string id;
    nlohmann::json iceParameters;
    nlohmann::json iceCandidates;
    nlohmann::json dtlsParameters;
};

struct ConnectTransportRequest {
    nlohmann::json dtlsParameters;
};

struct ProduceRequest {
    std::string transportId;
    std::string kind; // "video"
    nlohmann::json rtpParameters;
};

struct ProduceResponse {
    std::string id;
};

struct ConsumeRequest {
    std::string transportId;
    std::string producerId;
    nlohmann::json rtpCapabilities;
};

struct ConsumeResponse {
    std::string id;
    std::string producerId;
    std::string kind;
    nlohmann::json rtpParameters;
};

struct IceServersResponse {
    std::vector<nlohmann::json> iceServers;
};
```

- [ ] **Step 2: Write StreamController**

Each handler:
1. Parse request DTO
2. Validate auth (JWT required)
3. Call RoomManager/MediasoupManager
4. Return response DTO

- [ ] **Step 3: Add i18n keys**
- [ ] **Step 4: Register routes in Router.cpp**
- [ ] **Step 5: Build and run tests**
- [ ] **Step 6: Commit**

---

### Task 6: coturn Configuration

**Files:**
- Create: `deploy/coturn/turnserver.conf`
- Create: `deploy/coturn/README.md`

This task documents the coturn deployment. No C++ code changes — coturn runs as a separate process.

- [ ] **Step 1: Write turnserver.conf**

```ini
# coturn configuration for HUB32
listening-port=3478
tls-listening-port=443
relay-device=eth0
listening-ip=0.0.0.0
external-ip=YOUR_VPS_IP
min-port=40000
max-port=49999
use-auth-secret
static-auth-secret=GENERATE_WITH_OPENSSL_RAND
realm=hub32
cert=/etc/letsencrypt/live/turn.example.com/fullchain.pem
pkey=/etc/letsencrypt/live/turn.example.com/privkey.pem
no-tcp-relay
denied-peer-ip=10.0.0.0-10.255.255.255
denied-peer-ip=172.16.0.0-172.31.255.255
denied-peer-ip=192.168.0.0-192.168.255.255
no-rfc5780
log-file=/var/log/coturn/turnserver.log
```

- [ ] **Step 2: Add TURN credential generation to StreamController**

In `GET /api/v1/stream/ice-servers`:
```cpp
// Generate time-limited HMAC credentials for TURN
auto username = std::to_string(time(nullptr) + 3600) + ":hub32user";
auto credential = hmac_sha1(turnSecret, username);
// Return JSON:
// { "iceServers": [
//     { "urls": "stun:turn.example.com:3478" },
//     { "urls": "turn:turn.example.com:443?transport=tcp",
//       "username": "...", "credential": "..." }
// ]}
```

- [ ] **Step 3: Add turnSecret to ServerConfig**

```cpp
// ServerConfig.hpp:
std::string turnSecret;    // HMAC secret for TURN credential generation
std::string turnServerUrl; // e.g., "turn.example.com"
```

- [ ] **Step 4: Commit**

---

## Phase 3 Completion Checklist

- [ ] mediasoup worker builds as static library (Meson → CMake)
- [ ] MediasoupManager spawns worker threads, communicates via FlatBuffers
- [ ] RoomManager creates 1 Router per location (on-demand)
- [ ] StreamController: 7 signaling endpoints with auth
- [ ] TURN credential generation (HMAC time-limited)
- [ ] coturn configuration documented
- [ ] All existing tests still pass
- [ ] Basic mediasoup manager test passes
