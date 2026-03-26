# HUB32 - School Management Platform: Deployment Plan

## Architecture: Full School (not just 1 classroom)

```
                         INTERNET
                            |
                     +----- VPS -----+
                     |               |
              +------+------+ +------+------+
              | hub32api    | | coturn      |
              | (REST API + | | (TURN relay)|
              | mediasoup   | |             |
              | SFU)        | +-------------+
              +------+------+
                     |
          +----------+----------+
          |          |          |
     Room A     Room B     Room C ...
     (Class 1)  (Class 2)  (Lab 1)
     30 PCs     30 PCs     20 PCs
```

### Core Components

```
hub32api-server (single C++ binary on VPS)
|
+-- REST API (cpp-httplib)
|   +-- /api/v1/auth           JWT RS256 auth
|   +-- /api/v1/schools        School management
|   +-- /api/v1/locations      Rooms/Labs/Buildings
|   +-- /api/v1/computers      All computers across school
|   +-- /api/v1/teachers       Teacher accounts & permissions
|   +-- /api/v1/stream/*       WebRTC signaling
|   +-- /api/v1/features       Feature control (lock/demo/msg)
|   +-- /api/v2/batch/*        Batch operations
|   +-- /api/v2/metrics        Prometheus metrics
|   +-- /api/v2/audit          Audit log queries
|
+-- mediasoup C++ worker (embedded, same process)
|   +-- 1 Worker per CPU core
|   +-- 1 Router per Room
|   +-- WebRtcTransport per student (producer)
|   +-- WebRtcTransport per teacher (consumer)
|   +-- H.264 SFU forwarding (no transcode)
|
+-- SQLite databases
|   +-- school.db      Computers, rooms, teachers, permissions
|   +-- audit.db       All actions logged
|   +-- tokens.db      JWT revocation persistent
|
+-- coturn (separate process, same VPS)
    +-- STUN/TURN for NAT traversal
    +-- UDP relay for symmetric NAT
    +-- TLS on port 443 for firewall bypass
```

```
hub32-agent (C++ binary on each student PC)
|
+-- DXGI Desktop Duplication (screen capture)
+-- FFmpeg NVENC/QSV/x264 (H.264 encode)
+-- libdatachannel (WebRTC producer)
+-- Feature handlers (WinAPI direct)
|   +-- ScreenLock (fullscreen overlay)
|   +-- InputLock (BlockInput)
|   +-- MessageDisplay (MessageBoxW)
|   +-- PowerControl (ExitWindowsEx)
|   +-- AppLauncher (ShellExecute)
|   +-- WebOpener (ShellExecute)
+-- REST client (poll hub32api for commands)
+-- Auto-update capability
+-- Windows Service (auto-start)
```

```
Teacher Web Dashboard (Browser, zero install)
|
+-- mediasoup-client.js (WebRTC consumer)
+-- Grid view: all students in selected room
+-- Feature control buttons
+-- Multi-room switching
+-- Role-based access (admin sees all rooms, teacher sees assigned rooms)
```

---

## VPS Requirements

### For 1 school (200 PCs, 10 rooms, 5 teachers concurrent)

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU | 4 cores | 8 cores |
| RAM | 8 GB | 16 GB |
| Bandwidth | 500 Mbps | 1 Gbps |
| Storage | 20 GB SSD | 50 GB SSD |
| OS | Ubuntu 22.04 | Ubuntu 24.04 |

### Bandwidth calculation (worst case: 1 teacher views all 200 PCs)

```
Student upload (H.264 720p simulcast 3 layers):
  Low:    150 kbps
  Medium: 500 kbps
  High:   2,500 kbps
  Total per student: ~3,150 kbps ingress

200 students x 3.15 Mbps = 630 Mbps ingress

Teacher viewing 200 thumbnails (low layer only):
200 x 150 kbps = 30 Mbps egress per teacher

Teacher viewing 1 student fullscreen (high layer):
1 x 2,500 kbps = 2.5 Mbps egress

5 teachers concurrent: ~165 Mbps egress max
Total: ~800 Mbps peak
```

---

## Data Model (SQLite)

```sql
-- school.db

CREATE TABLE schools (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    address TEXT,
    created_at INTEGER NOT NULL
);

CREATE TABLE locations (
    id TEXT PRIMARY KEY,
    school_id TEXT NOT NULL REFERENCES schools(id),
    name TEXT NOT NULL,           -- "Phong May 1", "Lab A"
    building TEXT,                -- "Toa A"
    floor INTEGER,
    capacity INTEGER,
    type TEXT DEFAULT 'classroom' -- classroom, lab, office
);

CREATE TABLE computers (
    id TEXT PRIMARY KEY,
    location_id TEXT NOT NULL REFERENCES locations(id),
    hostname TEXT NOT NULL,
    mac_address TEXT,
    ip_last_seen TEXT,
    agent_version TEXT,
    last_heartbeat INTEGER,
    state TEXT DEFAULT 'offline', -- online, offline, locked, demo
    position_x INTEGER,          -- seat position in room
    position_y INTEGER
);

CREATE TABLE teachers (
    id TEXT PRIMARY KEY,
    username TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,  -- argon2id
    full_name TEXT NOT NULL,
    role TEXT DEFAULT 'teacher',  -- admin, teacher
    created_at INTEGER NOT NULL
);

CREATE TABLE teacher_locations (
    teacher_id TEXT REFERENCES teachers(id),
    location_id TEXT REFERENCES locations(id),
    PRIMARY KEY (teacher_id, location_id)
);

CREATE TABLE active_sessions (
    computer_id TEXT PRIMARY KEY REFERENCES computers(id),
    user_login TEXT,
    user_fullname TEXT,
    session_start INTEGER,
    producer_id TEXT,             -- mediasoup producer ID
    transport_id TEXT             -- mediasoup transport ID
);

-- audit.db

CREATE TABLE audit_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    teacher_id TEXT,
    action TEXT NOT NULL,         -- lock, unlock, message, power, view
    target_type TEXT,             -- computer, location, school
    target_id TEXT,
    details TEXT,                 -- JSON extra info
    ip_address TEXT
);

-- tokens.db

CREATE TABLE revoked_tokens (
    jti TEXT PRIMARY KEY,
    revoked_at INTEGER NOT NULL,
    expires_at INTEGER NOT NULL   -- auto-cleanup after expiry
);
```

---

## Current Status Assessment

### hub32api-server: ~35% complete

| Component | Status | Lines | Real Code |
|-----------|--------|-------|-----------|
| REST API framework | DONE | ~2,000 | 95% real |
| JWT Auth (RS256+HS256) | DONE but buggy | ~800 | 80% (security issues) |
| Controllers v1 | DONE (mock data) | ~1,500 | 70% (stubs in core) |
| Controllers v2 | DONE (mock data) | ~800 | 60% |
| Middleware | DONE (race conditions) | ~600 | 85% |
| Config system | DONE | ~500 | 90% |
| Plugin system | DONE (framework) | ~400 | 90% |
| Hub32CoreWrapper | STUB | ~100 | 0% |
| ConnectionPool | STUB | ~200 | 30% (metadata only) |
| AuditLog | PARTIAL (SQLite bugs) | ~200 | 60% |
| mediasoup integration | NOT STARTED | 0 | 0% |
| School/Location model | NOT STARTED | 0 | 0% |
| Teacher management | NOT STARTED | 0 | 0% |
| WebRTC signaling API | NOT STARTED | 0 | 0% |
| Tests | 10% enabled | ~300 | 30% |

### hub32-agent: ~95% complete

| Component | Status | Lines | Real Code |
|-----------|--------|-------|-----------|
| DXGI Screen Capture | DONE | 703 | 100% |
| GDI Fallback | DONE | included | 100% |
| ScreenLock | DONE | 338 | 100% |
| InputLock | DONE | 121 | 100% |
| MessageDisplay | DONE | 234 | 100% |
| PowerControl | DONE | 184 | 100% |
| CommandDispatcher | DONE | 113 | 100% |
| AgentClient (REST) | DONE | 437 | 100% |
| WinServiceAdapter | DONE | 292 | 100% |
| Config (JSON+Registry) | DONE | 185 | 100% |
| FFmpeg H.264 encode | NOT STARTED | 0 | 0% |
| libdatachannel WebRTC | NOT STARTED | 0 | 0% |

### Teacher Web Dashboard: 0% (not started)

---

## Implementation Phases

### PHASE 0: Fix Critical Security (Week 1)
Priority: MUST DO FIRST before any internet deployment

- [ ] Fix JWT: remove HS256 fallback, RS256 only, fail if no key
- [ ] Fix JWT secret generation: use OpenSSL RAND_bytes not mt19937
- [ ] Remove hardcoded admin role in AuthController.cpp:102
- [ ] Implement proper teacher/password store (SQLite + argon2id)
- [ ] Implement TLS in HttpServer (cpp-httplib SSLServer)
- [ ] Fix rate limit race condition (std::atomic::fetch_add)
- [ ] Fix TokenStore timestamp mixing (steady_clock vs system_clock)
- [ ] Add input validation bounds for all endpoints
- [ ] Fix AuditLog SQLite error handling (check all return codes)
- [ ] Persistent token revocation (SQLite tokens.db)
- [ ] Config validation: fail on error, not warn

### PHASE 1: Database + School Model (Week 2)
Priority: Foundation for multi-room management

- [ ] Create SQLite schema (school.db, audit.db, tokens.db)
- [ ] SchoolRepository: CRUD schools
- [ ] LocationRepository: CRUD locations (rooms/labs)
- [ ] ComputerRepository: CRUD computers, state tracking
- [ ] TeacherRepository: CRUD teachers, password hashing
- [ ] TeacherLocationRepository: assign teachers to rooms
- [ ] Replace Hub32CoreWrapper mock with SQLite-backed directory
- [ ] Replace ComputerPlugin mock data with real database queries
- [ ] API: POST/GET/PUT/DELETE /api/v1/schools
- [ ] API: POST/GET/PUT/DELETE /api/v1/locations
- [ ] API: POST/GET/PUT/DELETE /api/v1/teachers
- [ ] API: GET /api/v1/locations/:id/computers (list PCs in room)
- [ ] Role-based access: admin=all rooms, teacher=assigned rooms only

### PHASE 2: Agent ↔ Server Communication (Week 3)
Priority: Get real computer data flowing

- [ ] Agent registration: POST /api/v1/agents/register
- [ ] Agent heartbeat: POST /api/v1/agents/heartbeat
- [ ] Agent command polling: GET /api/v1/agents/commands
- [ ] Agent command result: POST /api/v1/agents/commands/:id/result
- [ ] Server tracks agent state (online/offline via heartbeat timeout)
- [ ] ComputerRepository updates state from agent heartbeats
- [ ] Feature dispatch: server queues commands, agent polls and executes
- [ ] Feature result: agent reports success/failure back
- [ ] Test: agent registers → server shows computer online → send lock → agent locks screen

### PHASE 3: mediasoup C++ SFU Integration (Week 4-5)
Priority: Real-time screen streaming

- [ ] Clone mediasoup worker source as submodule
- [ ] Build libmediasoup-worker as static library (Meson external project)
- [ ] Link into hub32api-server
- [ ] MediasoupManager class:
    - [ ] Spawn worker thread with mediasoup_worker_run()
    - [ ] Implement ChannelReadFn/ChannelWriteFn callbacks
    - [ ] FlatBuffers message serialization/deserialization
- [ ] RoomManager class:
    - [ ] 1 Router per location (room)
    - [ ] Create/destroy routers on demand
- [ ] WebRTC signaling API:
    - [ ] POST /api/v1/stream/transport (create WebRtcTransport, return ICE+DTLS)
    - [ ] POST /api/v1/stream/transport/:id/connect (DTLS connect)
    - [ ] POST /api/v1/stream/produce (create Producer, return producerId)
    - [ ] POST /api/v1/stream/consume (create Consumer for teacher)
    - [ ] DELETE /api/v1/stream/transport/:id (cleanup)
- [ ] coturn deployment:
    - [ ] Install coturn on VPS
    - [ ] Configure TURN with TLS on port 443
    - [ ] Generate TURN credentials from hub32api (time-limited HMAC)
    - [ ] API: GET /api/v1/stream/ice-servers (return STUN+TURN URLs)

### PHASE 4: Agent H.264 WebRTC Producer (Week 5-6)
Priority: Student screen → SFU

- [ ] Integrate FFmpeg libavcodec into agent build
- [ ] DXGI frame → NV12 color conversion (D3D11 compute shader or libyuv)
- [ ] H.264 encode pipeline:
    - [ ] Try NVENC first (GPU, lowest latency)
    - [ ] Fallback QSV (Intel iGPU)
    - [ ] Fallback x264 ultrafast (CPU)
- [ ] Integrate libdatachannel into agent build
- [ ] WebRTC producer flow:
    - [ ] Agent calls POST /api/v1/stream/transport → get ICE+DTLS params
    - [ ] Agent calls POST /api/v1/stream/transport/:id/connect → DTLS handshake
    - [ ] Agent calls POST /api/v1/stream/produce → get producerId
    - [ ] Agent sends H.264 RTP packets via WebRTC
- [ ] Simulcast: 3 layers (150kbps thumbnail, 500kbps medium, 2500kbps full)
- [ ] Adaptive: reduce quality when bandwidth constrained
- [ ] Auto-reconnect on network interruption

### PHASE 5: Teacher Web Dashboard (Week 6-7)
Priority: Teacher can see and control

- [ ] Static web app served by hub32api (or separate nginx)
- [ ] Login page → JWT token stored in localStorage
- [ ] Room selector (shows assigned rooms for this teacher)
- [ ] Grid view:
    - [ ] mediasoup-client.js for WebRTC consumption
    - [ ] Each student = 1 <video> element
    - [ ] Thumbnail mode: consume low simulcast layer
    - [ ] Click to enlarge: switch to high layer
    - [ ] Student name + login status overlay
- [ ] Feature control panel:
    - [ ] Lock all / Lock selected
    - [ ] Send message (text input → all/selected)
    - [ ] Demo mode (teacher screen broadcast)
    - [ ] Power off / Reboot
    - [ ] Open website / Start app (URL/path input)
- [ ] Multi-room tabs (teacher can switch between assigned rooms)
- [ ] Admin panel (admin role):
    - [ ] Manage schools, locations, computers
    - [ ] Manage teachers, assign to rooms
    - [ ] View audit log
    - [ ] System metrics (Prometheus dashboard link)

### PHASE 6: Demo Mode - Teacher Screen Broadcast (Week 7-8)
Priority: Teacher shows their screen to all students

- [ ] Teacher clicks "Demo" → browser captures teacher screen (getDisplayMedia)
- [ ] Browser produces via mediasoup-client.js → SFU
- [ ] SFU creates consumers for ALL students in room
- [ ] Agent receives teacher stream via libdatachannel consumer
- [ ] Agent displays fullscreen overlay (similar to ScreenLock but with video)
- [ ] Or: student browser opens fullscreen video player (if web-based student client)

### PHASE 7: Testing + Hardening (Week 8-9)
Priority: Production readiness

- [ ] Enable ALL disabled unit tests
- [ ] Refactor controllers for testability (inject dependencies, not httplib direct)
- [ ] Integration tests: full auth → stream → feature → audit flow
- [ ] Load test: 200 agents, 5 teachers, concurrent feature commands
- [ ] Stress test mediasoup: 200 producers, measure CPU/memory/bandwidth
- [ ] Security audit: JWT, TLS, input validation, SQL injection
- [ ] Penetration test: TURN credentials, WebRTC DTLS, REST API fuzzing
- [ ] Error recovery: agent reconnect, SFU worker crash recovery
- [ ] Graceful degradation: fallback when NVENC unavailable
- [ ] Monitoring: Prometheus + Grafana dashboard for VPS

### PHASE 8: Deployment + Operations (Week 9-10)
Priority: Go live

- [ ] Docker compose for VPS (hub32api + coturn + nginx)
- [ ] TLS certificates (Let's Encrypt auto-renewal)
- [ ] Agent MSI installer (silent install on student PCs)
- [ ] Agent auto-update mechanism (check version on heartbeat)
- [ ] Backup strategy: SQLite WAL + daily backup to S3/GCS
- [ ] Log rotation (spdlog rotating file sink)
- [ ] Domain setup: api.school.hub32.io, turn.school.hub32.io
- [ ] Firewall rules: 443 (HTTPS+TURN TLS), 3478 (STUN), 40000-49999 (WebRTC UDP)
- [ ] Documentation: admin guide, teacher guide, troubleshooting
- [ ] Pilot: 1 room first, then expand to full school

---

## Final Architecture When Complete

```
VPS (Ubuntu 24.04, 8 cores, 16GB RAM, 1Gbps)
|
+-- nginx (reverse proxy, TLS termination)
|   +-- hub32.school.example.com → hub32api:11081
|   +-- Static files: teacher web dashboard
|
+-- hub32api-server (C++ binary, systemd service)
|   +-- REST API on port 11081 (behind nginx)
|   +-- mediasoup worker threads (1 per core, embedded)
|   +-- SQLite: school.db, audit.db, tokens.db
|   +-- JWT RS256 with RSA 4096-bit keys
|   +-- spdlog → /var/log/hub32api/
|
+-- coturn (TURN relay, systemd service)
|   +-- STUN: UDP 3478
|   +-- TURN TLS: TCP 443
|   +-- TURN UDP relay: 40000-49999
|   +-- Credentials: time-limited HMAC from hub32api
|
+-- Prometheus + Grafana (optional, monitoring)

School Network (each classroom)
|
+-- 30x Student PCs (Windows 10/11)
    +-- hub32-agent.exe (Windows Service, auto-start)
    +-- DXGI capture → H.264 NVENC → WebRTC → VPS
    +-- Feature execution (lock/message/power/app)
    +-- REST polling hub32api for commands
    +-- Auto-reconnect, auto-update

Teachers (anywhere with internet)
|
+-- Browser (Chrome/Edge/Firefox)
    +-- hub32.school.example.com
    +-- Login → select room → view grid → control features
    +-- WebRTC consumer via mediasoup-client.js
    +-- Zero install, works on laptop/tablet/phone
```

### Technology Stack Summary

| Layer | Technology | License | Size |
|-------|-----------|---------|------|
| REST API | cpp-httplib | BSD | ~100KB |
| JSON | nlohmann/json | MIT | ~50KB |
| Logging | spdlog | MIT | ~200KB |
| Auth | jwt-cpp + OpenSSL | MIT/Apache | ~3MB |
| Database | SQLite3 | Public Domain | ~1MB |
| SFU | mediasoup C++ worker | ISC | ~2MB |
| TURN | coturn | BSD | ~5MB |
| Screen Capture | DXGI (Windows API) | N/A | 0 |
| Video Encode | FFmpeg (NVENC/QSV/x264) | LGPL | ~5MB |
| WebRTC Client | libdatachannel | MPL-2.0 | ~500KB |
| Web Dashboard | vanilla JS + mediasoup-client | ISC | ~200KB |
| TLS Proxy | nginx | BSD | ~2MB |

**Total server binary: ~12MB. Total agent binary: ~12MB. Zero Qt. Zero VNC.**
