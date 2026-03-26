# Phase 7: Testing + Hardening — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve production readiness through comprehensive testing, dependency injection refactoring for testability, load testing (200 agents, 5 teachers), security audit, and monitoring setup.

**Architecture:** Refactor controllers to accept interfaces (not concrete types) for testability. Add integration tests for full auth→stream→feature→audit flows. Load test with simulated agents. Deploy Prometheus metrics endpoint + Grafana dashboard.

**Tech Stack:** GoogleTest, GoogleMock, custom load testing harness (C++), Prometheus, Grafana

**Depends on:** Phase 6 (all features implemented)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `src/api/v1/controllers/*.cpp` | Dependency injection refactor |
| Create | `tests/mocks/MockRepository.hpp` | Mock database repositories |
| Modify | `tests/unit/api/v1/*.cpp` | Enable disabled controller tests |
| Create | `tests/integration/test_full_flow.cpp` | Full auth→stream→feature→audit |
| Create | `tests/load/load_test_agents.cpp` | Simulate 200 agents |
| Create | `tests/load/load_test_teachers.cpp` | Simulate 5 concurrent teachers |
| Create | `tests/security/test_jwt_security.cpp` | JWT bypass attempts |
| Create | `tests/security/test_sql_injection.cpp` | SQL injection attempts |
| Create | `tests/security/test_input_fuzzing.cpp` | Input validation fuzzing |
| Modify | `src/plugins/metrics/MetricsPlugin.cpp` | Prometheus format export |
| Create | `deploy/grafana/dashboard.json` | Grafana dashboard template |
| Create | `deploy/prometheus/prometheus.yml` | Prometheus config |

---

### Task 1: Controller Dependency Injection Refactor

**Why:** Currently controllers take concrete types (PluginRegistry&, JwtAuth&). For unit testing, they need interfaces.

- [ ] **Step 1: Create ISchoolRepository, ILocationRepository interfaces**
- [ ] **Step 2: Make controllers accept interface references**
- [ ] **Step 3: Create MockRepository implementations**
- [ ] **Step 4: Enable and fix disabled controller unit tests**
- [ ] **Step 5: Run all tests**
- [ ] **Step 6: Commit**

---

### Task 2: Integration Tests

- [ ] **Step 1: Write test_full_flow.cpp**

```
1. Start server in-process (or via subprocess)
2. Login → get token
3. Create school → create location → create teacher → assign location
4. Register agent → verify computer appears
5. Send feature command → agent polls → report result
6. Stream: create transport → produce → consume
7. Verify audit log entries
8. Logout → verify token revoked
```

- [ ] **Step 2: Write test for concurrent teacher operations**
- [ ] **Step 3: Write test for agent reconnection after disconnect**
- [ ] **Step 4: Run all integration tests**
- [ ] **Step 5: Commit**

---

### Task 3: Load Testing

- [ ] **Step 1: Write load_test_agents.cpp**

Simulates 200 agents:
- Register concurrently (staggered over 30s)
- Heartbeat every 30s
- Poll commands every 5s
- Measure: registration latency, heartbeat throughput, command delivery latency

- [ ] **Step 2: Write load_test_teachers.cpp**

Simulates 5 teachers:
- Login
- List computers in room
- Send feature commands (lock/unlock/message)
- Subscribe to streams (consume)
- Measure: API latency p50/p95/p99, stream setup time

- [ ] **Step 3: Run load tests, record results**
- [ ] **Step 4: Optimize bottlenecks found**
- [ ] **Step 5: Commit**

---

### Task 4: Security Audit

- [ ] **Step 1: JWT security tests**

- Try accessing protected endpoints without token → 401
- Try with expired token → 401
- Try with revoked token → 401
- Try with HS256-signed token against RS256 server → 401
- Try with modified payload (different role) → 401
- Try with empty algorithm header → 401

- [ ] **Step 2: SQL injection tests**

- Try injecting SQL in all string parameters
- Verify prepared statements prevent all injection
- Test: `'; DROP TABLE schools;--` in school name → safe

- [ ] **Step 3: Input fuzzing**

- Send malformed JSON bodies
- Send extremely long strings
- Send unicode edge cases
- Send binary data as JSON
- Verify all cases return proper error responses (400/413/415)

- [ ] **Step 4: CORS verification**

- Verify only whitelisted origins get CORS headers
- Verify credentials not sent to unauthorized origins

- [ ] **Step 5: Document findings and fixes**
- [ ] **Step 6: Commit**

---

### Task 5: Prometheus Metrics

- [ ] **Step 1: Enhance MetricsPlugin with Prometheus format**

```
# TYPE hub32_http_requests_total counter
hub32_http_requests_total{method="GET",status="200"} 1234
hub32_http_requests_total{method="POST",status="401"} 56

# TYPE hub32_agents_online gauge
hub32_agents_online 42

# TYPE hub32_streams_active gauge
hub32_streams_active 15

# TYPE hub32_http_request_duration_seconds histogram
hub32_http_request_duration_seconds_bucket{le="0.01"} 100
hub32_http_request_duration_seconds_bucket{le="0.05"} 200
```

- [ ] **Step 2: Create Grafana dashboard JSON**
- [ ] **Step 3: Create Prometheus config**
- [ ] **Step 4: Commit**

---

### Task 6: Error Recovery Testing

- [ ] **Step 1: Test agent reconnect after server restart**
- [ ] **Step 2: Test mediasoup worker crash recovery**
- [ ] **Step 3: Test database corruption handling**
- [ ] **Step 4: Test graceful degradation (no NVENC → x264 fallback)**
- [ ] **Step 5: Commit**

---

## Phase 7 Completion Checklist

- [ ] All controller unit tests enabled and passing
- [ ] Integration tests: full auth→stream→feature→audit flow
- [ ] Load test: 200 agents + 5 teachers concurrent
- [ ] Security audit: JWT, SQL injection, input fuzzing, CORS
- [ ] Prometheus metrics endpoint with proper format
- [ ] Grafana dashboard template
- [ ] Error recovery tested and documented
- [ ] All 30+ tests passing
