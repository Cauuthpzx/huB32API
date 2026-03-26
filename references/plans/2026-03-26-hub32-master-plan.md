# HUB32 School Management Platform — Master Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform hub32api from a 35%-complete REST API with mock data into a production-ready school management platform with real-time WebRTC screen streaming (mediasoup SFU), SQLite-backed multi-school/multi-room data model, and a zero-install teacher web dashboard.

**Architecture:** C++ REST API server (cpp-httplib) with embedded mediasoup C++ SFU worker, SQLite3 databases (school.db, audit.db, tokens.db), C++ Windows agent (DXGI capture → H.264 → libdatachannel WebRTC), and vanilla JS teacher dashboard consuming WebRTC streams via mediasoup-client.js. Communication: HTTPS REST + WebRTC media. NAT traversal via coturn TURN/TLS.

**Tech Stack:** C++17, cpp-httplib, nlohmann/json, spdlog, jwt-cpp, OpenSSL, SQLite3, mediasoup C++ worker (FlatBuffers), libdatachannel, FFmpeg (NVENC/QSV/x264), coturn, vanilla JS + mediasoup-client.js, nginx, Docker.

---

## Current State (2026-03-26)

| Component | Status | Real % |
|-----------|--------|--------|
| hub32api REST framework | DONE | 95% |
| JWT Auth (RS256) | DONE | 90% |
| Controllers v1/v2 | DONE (mock data) | 65% |
| Middleware chain | DONE | 85% |
| Agent binary (5 features) | DONE | 95% |
| Agent ↔ Server comm | DONE (basic) | 90% |
| SQLite audit log | DONE | 80% |
| School/Location/Teacher model | NOT STARTED | 0% |
| mediasoup SFU | NOT STARTED | 0% |
| H.264 encode + WebRTC producer | NOT STARTED | 0% |
| Teacher web dashboard | NOT STARTED | 0% |
| coturn deployment | NOT STARTED | 0% |

## Phase Overview

Each phase is a separate plan document. Execute them **sequentially** — each phase depends on the prior.

| Phase | Plan Document | Duration | Dependencies |
|-------|--------------|----------|-------------|
| **Phase 0** | [Security Hardening](2026-03-26-phase0-security.md) | Week 1 | None (MUST DO FIRST) |
| **Phase 1** | [Database + School Model](2026-03-26-phase1-database.md) | Week 2 | Phase 0 |
| **Phase 2** | [Agent Communication Enhancement](2026-03-26-phase2-agent-comm.md) | Week 3 | Phase 1 |
| **Phase 3** | [mediasoup SFU Integration](2026-03-26-phase3-mediasoup.md) | Week 4-5 | Phase 1 |
| **Phase 4** | [Agent H.264 WebRTC Producer](2026-03-26-phase4-webrtc-producer.md) | Week 5-6 | Phase 3 |
| **Phase 5** | [Teacher Web Dashboard](2026-03-26-phase5-dashboard.md) | Week 6-7 | Phase 3, Phase 4 |
| **Phase 6** | [Demo Mode](2026-03-26-phase6-demo.md) | Week 7-8 | Phase 5 |
| **Phase 7** | [Testing + Hardening](2026-03-26-phase7-testing.md) | Week 8-9 | Phase 6 |
| **Phase 8** | [Deployment + Operations](2026-03-26-phase8-deployment.md) | Week 9-10 | Phase 7 |

## Build Commands (reference)

```bash
export PATH="/c/msys64/mingw64/bin:$PATH"
cd "c:/Users/Admin/Desktop/veyon/hub32api"
cmake --preset debug
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
```

## Key Conventions

- **Language:** C++17, MinGW/GCC 15
- **Error handling:** `Result<T>` everywhere, no exceptions in public API
- **Threading:** `std::mutex` / `std::shared_mutex` for read-heavy data
- **Logging:** spdlog only, never `std::cout`
- **i18n:** All user-facing strings via `tr(lang, "key")` pattern
- **Auth:** JWT RS256 only, never HS256 in production
- **Database:** SQLite3, WAL mode, prepared statements (no string concat)
- **Testing:** GoogleTest, TDD where applicable
- **Commits:** Frequent, small, descriptive (feat/fix/test/refactor prefix)

## Reference: Source Repo at `HUB32_Development_Plan.docx`

The authoritative spec is `c:/Users/Admin/Desktop/veyon/HUB32_Development_Plan.docx`. This master plan implements all 12 sections of that document.
